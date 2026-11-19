using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;

namespace InfiniteProps
{
    public sealed partial class ModelTexture
    {
        public Texture2D ToTexture2D(bool crisp = false)
        {
            if (Rgba.Length == 0) throw new InvalidOperationException("Missing model texture pixels");
            var texture = GeneratedImage.Upload(Rgba, Width, Height, RowStride, false);
            texture.filterMode = crisp ? FilterMode.Point : FilterMode.Bilinear;
            texture.wrapMode = Repeats ? TextureWrapMode.Repeat : TextureWrapMode.Clamp;
            return texture;
        }
    }

    public sealed partial class GeneratedModel
    {
        public Mesh ToMesh()
        {
            var positions = new List<Vector3>();
            var normals = new List<Vector3>();
            var tangents = new List<Vector4>();
            var uvs = new List<Vector2>();
            var triangles = new List<int[]>();
            foreach (var s in Surfaces)
            {
                int start = positions.Count;
                for (int i = 0; i < s.Positions.Length / 3; ++i)
                {
                    positions.Add(new Vector3(s.Positions[3*i], s.Positions[3*i+1], -s.Positions[3*i+2]) / UnitsPerMeter);
                    normals.Add(new Vector3(s.Normals[3*i], s.Normals[3*i+1], -s.Normals[3*i+2]));
                    tangents.Add(new Vector4(s.Tangents[4*i], s.Tangents[4*i+1], -s.Tangents[4*i+2], -s.Tangents[4*i+3]));
                    uvs.Add(new Vector2(s.Uvs[2*i], 1 - s.Uvs[2*i+1]));
                }
                var indices = new int[s.Indices.Length];
                for (int j = 0; j < indices.Length; j += 3)
                {
                    indices[j] = start + s.Indices[j];
                    indices[j+1] = start + s.Indices[j+2];
                    indices[j+2] = start + s.Indices[j+1];
                }
                triangles.Add(indices);
            }
            var mesh = new Mesh();
            try
            {
                if (positions.Count > ushort.MaxValue) mesh.indexFormat = IndexFormat.UInt32;
                mesh.SetVertices(positions);
                mesh.SetNormals(normals);
                mesh.SetTangents(tangents);
                mesh.SetUVs(0, uvs);
                mesh.subMeshCount = triangles.Count;
                for (int i = 0; i < triangles.Count; ++i) mesh.SetTriangles(triangles[i], i, false);
                mesh.RecalculateBounds();
                return mesh;
            }
            catch { GeneratedImage.DestroyOwned(mesh); throw; }
        }
    }
}
