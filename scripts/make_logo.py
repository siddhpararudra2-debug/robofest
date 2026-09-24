from pathlib import Path
from PIL import Image, ImageDraw

size = 128
image = Image.new('RGBA', (size, size), (255, 255, 255, 0))
draw = ImageDraw.Draw(image)
draw.ellipse((2, 2, size - 2, size - 2), fill='white')
draw.ellipse((24, 24, size - 24, size - 24), fill='black')
draw.ellipse((37, 37, size - 37, size - 37), outline='white', width=10)
draw.ellipse((59, 59, 69, 69), fill='black')
image.save(Path(__file__).parent.parent / 'assets' / 'logo.webp', 'WEBP', quality=92)
