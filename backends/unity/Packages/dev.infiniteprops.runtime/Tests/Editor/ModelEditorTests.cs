using System.IO;
using System.Linq;
using UnityEditor.SceneManagement;
using UnityEngine.SceneManagement;
using NUnit.Framework;
using UnityEngine;

namespace InfiniteProps.Tests
{
    public class ModelEditorTests
    {
        [Test]
        public void ModelMeshAndTexture()
        {
            try { _ = Model.Library; }
            catch (System.NotSupportedException ex) when (ex.InnerException is System.DllNotFoundException)
            { Assert.Ignore("Install the editor plugin with package-native.py --models"); }
            string path = Path.GetFullPath(Path.Combine(Application.dataPath, "../PixelCorpus"));
            ModelCorpus.Run(path);
            using (var model = new Model(File.ReadAllBytes(Path.Combine(path, "model-crate.iprop3d"))))
            {
                var generated = model.Generate(5);
                var mesh = generated.ToMesh();
                var texture = generated.Textures[0].ToTexture2D(true);
                try
                {
                    Assert.AreEqual(generated.Surfaces.Length, mesh.subMeshCount);
                    Assert.AreEqual(generated.Statistics.VertexCount, (uint)mesh.vertexCount);
                    var vertices = mesh.vertices;
                    var normals = mesh.normals;
                    var uv = mesh.uv;
                    var tangents = mesh.tangents;
                    int start = 0;
                    foreach (var s in generated.Surfaces)
                    {
                        var indices = mesh.GetTriangles((int)s.Index);
                        for (int j = 0; j < indices.Length; j += 3) {
                            Assert.AreEqual(start + s.Indices[j], indices[j]);
                            Assert.AreEqual(start + s.Indices[j+2], indices[j+1]);
                            Assert.AreEqual(start + s.Indices[j+1], indices[j+2]);
                        }
                        for (int j = 0; j < s.Positions.Length/3; ++j) {
                            Assert.AreEqual(new Vector3(s.Positions[3*j], s.Positions[3*j+1], -s.Positions[3*j+2]) / generated.UnitsPerMeter, vertices[start+j]);
                            Assert.AreEqual(new Vector3(s.Normals[3*j], s.Normals[3*j+1], -s.Normals[3*j+2]), normals[start+j]);
                            Assert.AreEqual(new Vector2(s.Uvs[2*j], 1 - s.Uvs[2*j+1]), uv[start+j]);
                            Assert.AreEqual(-s.Tangents[4*j+3], tangents[start+j].w);
                        }
                        start += s.Positions.Length/3;
                    }
                    Assert.AreEqual(FilterMode.Point, texture.filterMode);
                    var bytes = texture.GetRawTextureData();
                    var raw = generated.Textures[0];
                    int stride = (int)raw.RowStride;
                    for (int y = 0; y < texture.height; ++y)
                        for (int x = 0; x < stride; ++x)
                            Assert.AreEqual(raw.Rgba[y*stride+x], bytes[(texture.height-y-1)*stride+x]);
                }
                finally { Object.DestroyImmediate(mesh); Object.DestroyImmediate(texture); }
                CheckLitPlate(generated);
            }
        }

        // The crate's cutout quad carries a raised panel in its authored normal map.
        static void CheckLitPlate(GeneratedModel generated)
        {
            var plate = generated.Surfaces.Single(s => s.AlphaMode == 1);
            var image = generated.Textures[plate.TextureIndex].Images.Single(i => i.Kind == ImageKind.Normal);
            var shader = Shader.Find("Hidden/InfiniteProps/ModelPlate");
            Assert.IsNotNull(shader);
            var scene = EditorSceneManager.NewPreviewScene();
            var previous = RenderTexture.active;
            var mesh = generated.ToMesh();
            var normals = image.ToTexture2D();
            var material = new Material(shader);
            var target = new RenderTexture(128, 128, 16, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear);
            var readback = new Texture2D(128, 128, TextureFormat.RGBA32, false, true);
            try
            {
                var indices = mesh.GetTriangles((int)plate.Index);
                mesh.subMeshCount = 1;
                mesh.SetTriangles(indices, 0);
                var quad = new GameObject("Raised panel", typeof(MeshFilter), typeof(MeshRenderer));
                SceneManager.MoveGameObjectToScene(quad, scene);
                quad.GetComponent<MeshFilter>().sharedMesh = mesh;
                material.SetTexture("_NormalMap", normals);
                quad.GetComponent<MeshRenderer>().sharedMaterial = material;
                var eye = new GameObject("Plate camera", typeof(Camera));
                SceneManager.MoveGameObjectToScene(eye, scene);
                eye.transform.position = new Vector3(0, 9 / generated.UnitsPerMeter, -1);
                var camera = eye.GetComponent<Camera>();
                camera.scene = scene;
                camera.enabled = false;
                camera.orthographic = true;
                camera.orthographicSize = 1 / generated.UnitsPerMeter;
                camera.clearFlags = CameraClearFlags.SolidColor;
                camera.backgroundColor = Color.black;
                camera.targetTexture = target;
                camera.Render();
                RenderTexture.active = target;
                readback.ReadPixels(new Rect(0, 0, 128, 128), 0, 0);
                // Sample through the centre of the panel, 4 of the skin's 16 pixels across.
                float upper = 0, lower = 1;
                for (int y = 0; y < 32; ++y)
                {
                    upper = Mathf.Max(upper, readback.GetPixel(32, 127-y).r);
                    lower = Mathf.Min(lower, readback.GetPixel(32, y).r);
                }
                Assert.Greater(upper, readback.GetPixel(32, 64).r + 0.1f, "Upper bevel faces the light");
                Assert.Less(lower, readback.GetPixel(32, 64).r - 0.1f, "Lower bevel faces away");
            }
            finally
            {
                RenderTexture.active = previous;
                EditorSceneManager.ClosePreviewScene(scene);
                Object.DestroyImmediate(mesh);
                Object.DestroyImmediate(normals);
                Object.DestroyImmediate(material);
                Object.DestroyImmediate(target);
                Object.DestroyImmediate(readback);
            }
        }
    }
}
