all: pcm

pcm: pcm.cpp
	g++ -g -O0 -o pcm pcm.cpp
