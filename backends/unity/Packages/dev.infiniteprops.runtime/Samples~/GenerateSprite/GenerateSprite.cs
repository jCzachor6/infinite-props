using UnityEngine;
using InfiniteProps;

public sealed class GenerateSprite : MonoBehaviour
{
    public IpropAsset asset;
    public SpriteRenderer target;
    private Sprite generated;
    void Start()
    {
        using (var program = asset.Load())
            generated = program.Generate(seed: 7).ToSprite(program.Metadata);
        target.sprite = generated;
    }
    void OnDestroy()
    {
        if (generated == null) return;
        Destroy(generated.texture);
        Destroy(generated);
    }
}
