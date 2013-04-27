OBJS = test.o jpeg_utils.o v4l2uvc.o
CC = gcc
DEBUG = -g
CFLAGS = -Wall -c $(DEBUG)
LFLAGS = -Wall $(DEBUG) -ljpeg -lcurl

electricity-monitoring: $(OBJS)
	$(CC) $(OBJS) $(LFLAGS) -o electricity-monitoring

test.o: test.c header.h huffman.h v4l2uvc.h
	$(CC) $(CFLAGS) test.c

jpeg_utils.o: jpeg_utils.c header.h huffman.h v4l2uvc.h
	$(CC) $(CFLAGS) jpeg_utils.c

v4l2uvc.o: v4l2uvc.c header.h huffman.h v4l2uvc.h
	$(CC) $(CFLAGS) v4l2uvc.c

clean:
	rm *.o *~ electricity-monitoring *.jpg
