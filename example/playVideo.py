from oav import *

uim = UiModule.createAndInitialize()

v = VideoStream()

v.open('small.mp4')

img = Image.create(uim.getUi())
img.setData(v.getPixels())
looping = False
v.play()

def setLooping(loop):
    v.setLooping(loop)

def setPlaying(loop):
    v.setPlaying(loop)

def seekToTime(time):
	v.seekToTime(time)

def restart():
	v.seekToTime(0)