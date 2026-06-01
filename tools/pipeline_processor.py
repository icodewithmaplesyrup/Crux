import os
import gc
import pandas as pd
import numpy as np
import open3d as o3d

# Define your RandLA-Net / Semantic3D class mappings
# Adjust these lists based on your model's specific training classes
SEMANTIC_CLASSES = {
    "Ground": [1, 2, 6],  # Man-made terrain, Natural terrain, Hardscape
    "Buildings": [5],     # Buildings
    "Props": [3, 4, 8],   # High/Low vegetation, Cars
    "Noise": [0, 7]       # Unlabeled, Scanning artifacts (will be discarded)
}

def process_mesh(pcd, name, output_dir, is_ground=False):
    """
    Cleans the point cloud, generates a visual mesh, and creates a UCX collision mesh.
    """
    if len(pcd.points) < 1000:
        print(f"[-] Skipping {name}: Not enough points.")
        return

    print(f"[*] Processing geometry for: {name}...")
    
    # Clean noise and estimate normals (required for Poisson)
    pcd, _ = pcd.remove_statistical_outlier(nb_neighbors=20, std_ratio=2.0)
    pcd.estimate_normals(search_param=o3d.geometry.KDTreeSearchParamHybrid(radius=0.2, max_nn=30))
    pcd.orient_normals_consistent_tangent_plane(k=15)

    # Reconstruct Surface
    print(f"    -> Generating visual mesh (SM_{name})...")
    # Ground needs to be smoother (lower depth), buildings need detail (higher depth)
    poisson_depth = 8 if is_ground else 9 
    visual_mesh, densities = o3d.geometry.TriangleMesh.create_from_point_cloud_poisson(pcd, depth=poisson_depth)
    
    # Clean up Poisson extrapolation artifacts
    densities = np.asarray(densities)
    visual_mesh.remove_vertices_by_mask(densities < np.quantile(densities, 0.05))
    
    # Save Visual Mesh
    o3d.io.write_triangle_mesh(os.path.join(output_dir, f"SM_{name}.obj"), visual_mesh)

    # Generate Collision Mesh
    print(f"    -> Generating collision mesh (UCX_{name}_01)...")
    if is_ground:
        # Aggressive decimation for the floor to ensure slide-hopping is buttery smooth
        target_triangles = max(1000, len(visual_mesh.triangles) // 50)
    else:
        target_triangles = max(5000, len(visual_mesh.triangles) // 20)
        
    collision_mesh = visual_mesh.simplify_quadric_decimation(target_number_of_triangles=target_triangles)
    
    # The _01 suffix is required by Unreal Engine for multiple convex hulls, 
    # though here we are just providing a single optimized mesh collider per asset.
    o3d.io.write_triangle_mesh(os.path.join(output_dir, f"UCX_{name}_01.obj"), collision_mesh)
    print(f"[+] Finished {name}.")

def run_semantic_pipeline(txt_path, labels_path, output_dir, chunk_size=10_000_000, voxel_size=0.05):
    """
    Streams the raw dataset, matches it with semantic labels, groups points by category, 
    and pipes them to the mesh generator.
    """
    os.makedirs(output_dir, exist_ok=True)
    print(f"[*] Loading semantic labels from: {labels_path}")
    # Assuming labels are saved as a 1D numpy array (.npy). 
    # A 258M int32 array takes ~1GB of RAM, which is safe to load entirely.
    all_labels = np.load(labels_path)
    
    # Dictionaries to hold our categorized point clouds
    categorized_pcds = {
        "Ground": o3d.geometry.PointCloud(),
        "Buildings": o3d.geometry.PointCloud(),
        "Props": o3d.geometry.PointCloud()
    }
    
    print(f"[*] Streaming raw point cloud: {txt_path}")
    chunks = pd.read_csv(
        txt_path, sep=' ', header=None, names=['x', 'y', 'z', 'i', 'r', 'g', 'b'],
        chunksize=chunk_size,
        dtype={'x': np.float32, 'y': np.float32, 'z': np.float32, 'r': np.uint8, 'g': np.uint8, 'b': np.uint8}
    )
    
    current_idx = 0
    
    for idx, chunk in enumerate(chunks):
        print(f"    -> Sorting chunk {idx + 1} by semantic class...")
        
        chunk_len = len(chunk)
        chunk_labels = all_labels[current_idx : current_idx + chunk_len]
        
        xyz = chunk[['x', 'y', 'z']].values
        rgb = chunk[['r', 'g', 'b']].values / 255.0
        
        # Route points to their respective categories based on the RandLA-Net labels
        for category, class_ids in SEMANTIC_CLASSES.items():
            if category == "Noise":
                continue # Discard
                
            # Create a boolean mask for the current category
            mask = np.isin(chunk_labels, class_ids)
            
            if np.any(mask):
                temp_pcd = o3d.geometry.PointCloud()
                temp_pcd.points = o3d.utility.Vector3dVector(xyz[mask])
                temp_pcd.colors = o3d.utility.Vector3dVector(rgb[mask])
                
                # Downsample immediately to save memory
                temp_pcd = temp_pcd.voxel_down_sample(voxel_size=voxel_size)
                categorized_pcds[category] += temp_pcd
                
        current_idx += chunk_len
        
        # Memory cleanup
        del chunk, xyz, rgb, chunk_labels, mask
        gc.collect()

    print("[*] Chunking complete. Starting mesh generation...")
    
    # Process each categorized point cloud into Unreal assets
    for category, pcd in categorized_pcds.items():
        if len(pcd.points) > 0:
            # Run a final downsample to merge chunk boundaries
            pcd = pcd.voxel_down_sample(voxel_size=voxel_size)
            is_ground = (category == "Ground")
            process_mesh(pcd, category, output_dir, is_ground=is_ground)

    print(f"[+] Pipeline execution complete. Assets saved to {output_dir}")

if __name__ == "__main__":
    # Define paths
    RAW_DATA = "../sg28_4.txt" 
    LABELS_DATA = "../sg28_4_labels.npy" # Output from your RandLA-Net inference
    OUTPUT_FOLDER = "../src/Maps/sg28_4_build/"
    
    # Execute
    run_semantic_pipeline(RAW_DATA, LABELS_DATA, OUTPUT_FOLDER)
