import os
from PIL import Image, ImageDraw, ImageFilter

def generate():
    out_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(out_dir, "..", ".."))
    icon_path = os.path.join(repo_root, "assets", "phide_icon.png")

    # 1. Welcome & Finish page banner (164 x 314 px, NSIS MUI2 standard)
    w, h = 164, 314
    banner = Image.new('RGB', (w, h), (14, 8, 28))
    draw = ImageDraw.Draw(banner)

    # Cosmic gradient
    for y in range(h):
        factor = y / float(h)
        r = int(14 + 26 * factor + 15 * (1.0 - abs(factor - 0.35) * 2 if 0.0 <= factor <= 0.7 else 0))
        g = int(8 + 12 * factor + 8 * (1.0 - abs(factor - 0.35) * 2 if 0.0 <= factor <= 0.7 else 0))
        b = int(28 + 55 * factor + 35 * (1.0 - abs(factor - 0.35) * 2 if 0.0 <= factor <= 0.7 else 0))
        draw.line([(0, y), (w, y)], fill=(min(255, r), min(255, g), min(255, b)))

    # Diagonal tech lines
    for i in range(-100, w + 100, 20):
        draw.line([(i, 0), (i + 80, h)], fill=(35, 18, 65), width=1)
    for j in range(-50, h + 100, 25):
        draw.line([(0, j), (w, j - 35)], fill=(30, 15, 60), width=1)

    # Subtle cosmic star dots
    stars = [(25, 40), (140, 28), (115, 75), (30, 120), (135, 145), (20, 210), (145, 230), (50, 280), (120, 295)]
    for sx, sy in stars:
        draw.point((sx, sy), fill=(180, 220, 255))
        draw.point((sx+1, sy), fill=(140, 180, 240))

    # Ambient glow behind logo
    glow = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    gdraw = ImageDraw.Draw(glow)
    gdraw.ellipse([w//2 - 55, 30, w//2 + 55, 140], fill=(120, 40, 220, 90))
    gdraw.ellipse([w//2 - 35, 50, w//2 + 35, 120], fill=(0, 220, 255, 60))
    glow = glow.filter(ImageFilter.GaussianBlur(16))
    banner.paste(glow, (0, 0), glow)

    # Overlay phide_icon
    if os.path.exists(icon_path):
        icon = Image.open(icon_path).convert('RGBA')
        icon = icon.resize((84, 84), Image.Resampling.LANCZOS)
        banner.paste(icon, ((w - 84) // 2, 45), icon)

    # Glowing dividing accent bar
    draw.line([(20, 148), (w - 20, 148)], fill=(120, 60, 210), width=2)
    draw.line([(35, 150), (w - 35, 150)], fill=(0, 225, 255), width=1)

    # Lower decorative cards
    for y_box in [175, 215, 255]:
        draw.rounded_rectangle([18, y_box, w - 18, y_box + 28], radius=4, fill=(24, 13, 48), outline=(60, 32, 105))

    # Micro accent dots inside cards
    for y_box, clr in zip([175, 215, 255], [(0, 229, 255), (192, 132, 252), (52, 211, 153)]):
        draw.ellipse([26, y_box + 10, 34, y_box + 18], fill=clr)
        draw.line([(42, y_box + 14), (w - 28, y_box + 14)], fill=(80, 45, 140), width=2)

    banner_file = os.path.join(out_dir, "welcome_banner.bmp")
    banner.save(banner_file, "BMP")

    # 2. Header logo bitmap (150 x 57 px, NSIS MUI2 header)
    hw, hh = 150, 57
    header = Image.new('RGB', (hw, hh), (255, 255, 255))
    hdraw = ImageDraw.Draw(header)

    # Soft modern gradient
    for y in range(hh):
        val = int(248 + 7 * (y / hh))
        hdraw.line([(0, y), (hw, y)], fill=(val, val - 1, val + 1))

    # Accent bar on the right
    hdraw.line([(hw - 60, 0), (hw, 0)], fill=(120, 60, 210), width=2)

    # Small icon in right corner
    if os.path.exists(icon_path):
        hicon = Image.open(icon_path).convert('RGBA')
        hicon = hicon.resize((42, 42), Image.Resampling.LANCZOS)
        header.paste(hicon, (hw - 50, (hh - 42) // 2), hicon)

    header_file = os.path.join(out_dir, "header_logo.bmp")
    header.save(header_file, "BMP")
    print(f"Branding assets saved to {banner_file} and {header_file}")

if __name__ == "__main__":
    generate()
