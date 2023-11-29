all: pcm

pcm: pcm.cpp host.cpp host.h
	g++ -g -O0 -o pcm pcm.cpp host.cpp
