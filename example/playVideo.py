from oav import *

uim = UiModule.createAndInitialize()

v = VideoStream()

v.open('small.mp4')

img = Image.create(uim.getUi())
img.setData(v.getPixels())

v.play()