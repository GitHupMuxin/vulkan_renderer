"""
将 R32_SFLOAT 格式的 KTX 文件转成灰度 PNG。
用法: python scripts/ktx_to_png.py data/Eu_map.ktx [输出.png]
"""
import struct
import sys

def ktx_to_png(ktx_path, png_path=None):
    if png_path is None:
        png_path = ktx_path.replace('.ktx', '.png')

    with open(ktx_path, 'rb') as f:
        data = f.read()

    # ---- 解析 KTX v1 头部 ----
    identifier = data[:12]
    if identifier != b'\xABKTX 11\xBB\r\n\x1A\n':
        print("Error: Not a valid KTX v1 file")
        return

    endianness        = struct.unpack_from('<I', data, 12)[0]
    glType            = struct.unpack_from('<I', data, 16)[0]
    glTypeSize        = struct.unpack_from('<I', data, 20)[0]
    glFormat          = struct.unpack_from('<I', data, 24)[0]
    glInternalFormat  = struct.unpack_from('<I', data, 28)[0]
    glBaseInternalFmt = struct.unpack_from('<I', data, 32)[0]
    pixelWidth        = struct.unpack_from('<I', data, 36)[0]
    pixelHeight       = struct.unpack_from('<I', data, 40)[0]
    pixelDepth        = struct.unpack_from('<I', data, 44)[0]
    numberOfArrayElem = struct.unpack_from('<I', data, 48)[0]
    numberOfFaces     = struct.unpack_from('<I', data, 52)[0]
    numberOfMipLevels = struct.unpack_from('<I', data, 56)[0]
    bytesOfKeyValue   = struct.unpack_from('<I', data, 60)[0]

    print(f"  Width: {pixelWidth}, Height: {pixelHeight}")
    print(f"  glTypeSize: {glTypeSize} bytes per component")

    # ---- 跳过 key-value 数据 ----
    pixel_offset = 64 + bytesOfKeyValue
    pixel_data = data[pixel_offset:]

    expected_size = pixelWidth * pixelHeight * glTypeSize
    if len(pixel_data) < expected_size:
        print(f"Warning: expected {expected_size} bytes, got {len(pixel_data)}")
        expected_size = len(pixel_data)

    pixel_data = pixel_data[:expected_size]

    # ---- float32 → 0-255 灰度 ----
    import numpy as np
    from PIL import Image

    arr = np.frombuffer(pixel_data, dtype=np.float32).reshape(pixelHeight, pixelWidth)

    # 打印统计
    finite_mask = np.isfinite(arr)
    print(f"  Finite range: [{arr[finite_mask].min():.6f}, {arr[finite_mask].max():.6f}]")
    print(f"  Inf pixels: {(~finite_mask).sum()} / {arr.size}")

    available = arr[np.isfinite(arr) & (arr > 0)]
    if len(available) > 0:
        lo = np.percentile(available, 1)
        hi = np.percentile(available, 99)
        print(f"  1st percentile: {lo:.4f}, 99th percentile: {hi:.4f}")
    else:
        lo, hi = 0, 1
    print(f"  total finite >0: {len(available)} / {arr.size}")

    # 参考 C++ 用的线性 clamp，这里直接映射 0.9 到 255
    arr = np.nan_to_num(arr, nan=0.0, posinf=0.0, neginf=0.0)
    arr = np.clip(arr, 0.0, 1.0)
    arr = (arr * 255.0).astype(np.uint8)

    # 单通道 → RGBA，四个通道都是同一个 R 值，方便直接看
    rgba = np.zeros((pixelHeight, pixelWidth, 4), dtype=np.uint8)
    rgba[:, :, 0] = arr   # R
    rgba[:, :, 1] = arr   # G
    rgba[:, :, 2] = arr   # B
    rgba[:, :, 3] = 255   # A

    img = Image.fromarray(rgba, mode='RGBA')
    img.save(png_path)
    print(f"Saved: {png_path}")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python scripts/ktx_to_png.py <input.ktx> [output.png]")
    else:
        ktx_to_png(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
