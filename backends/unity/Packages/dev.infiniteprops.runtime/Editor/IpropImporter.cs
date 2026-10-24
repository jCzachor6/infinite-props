using System.IO;
using UnityEditor.AssetImporters;
using UnityEngine;

namespace InfiniteProps.Editor
{
    [ScriptedImporter(1, "iprop")]
    public sealed class IpropImporter : ScriptedImporter
    {
        [SerializeField] private int seed;
        public override void OnImportAsset(AssetImportContext context)
        {
            var source = File.ReadAllBytes(context.assetPath);
            using (var program = new Program(source))
            {
                var image = program.Generate(unchecked((uint)seed));
                var asset = ScriptableObject.CreateInstance<IpropAsset>();
                asset.SetBytes(source);
                var texture = image.ToTexture2D();
                texture.name = Path.GetFileNameWithoutExtension(context.assetPath);
                context.AddObjectToAsset("source", asset, texture);
                context.AddObjectToAsset("preview", texture);
                context.SetMainObject(asset);
            }
        }
    }
}
