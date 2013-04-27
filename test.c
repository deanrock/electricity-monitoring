#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <pthread.h>
#include <jpeglib.h>
#include <syslog.h>
#include "v4l2uvc.h"
#include "header.h"
#include <curl/curl.h>

int take_pictures = 0, aoi_x=-1, aoi_y=-1, aoi_range=-1;


int compress_yuyv_to_jpeg(struct vdIn *vd, unsigned char *buffer, int size, int quality);

char *dev = "/dev/video1", *s, *area_of_interest=NULL, *url=NULL;
int width = 640, height = 480, fps = 5, format = V4L2_PIX_FMT_MJPEG, i;
struct vdIn *videoIn;
static time_t start = 0;
static int counter = 0;
static int fpsX = 0;
char *buf;

char http_req_path[250];

//CURL
CURL *curl;
CURLcode res;

void make_http_request(const char* path) {
	curl = curl_easy_init();

	if (curl) {
		curl_easy_setopt(curl, CURLOPT_URL, path);

		res = curl_easy_perform(curl);

		if (res != CURLE_OK) {
			fprintf(stderr, "curl_easy_perform() failed: %s\n",
              curl_easy_strerror(res));
		}

		curl_easy_cleanup(curl);
	}

	return 0;
}

//CIRCULAR BUFFER
int circular_buffer[10];
int cb_start = 0, cb_end = 0, cb_active = 0;

void push_to_buffer(int i) {
	circular_buffer[cb_end] = i;
	cb_end = (cb_end + 1) % 10;

	if (cb_active < 10) {
		cb_active++;
	}else{
		cb_start = (cb_start+1)%10;
	}
}

//process a frame
void do_my_thing(struct vdIn *vd) {
	if(start==0){ 
			start = time(NULL);
		   counter = 0;
	}


    unsigned char *yuyv;
    int z;
	
    yuyv = vd->framebuffer;

    z = 0;
    int ratio=0;
    int count=0;
	
	int i_height, x;
	//printf("%d, %d\n", vd->height, vd->width);
    for(i_height = 0; i_height < vd->height;i_height++) {
        for(x = 0; x < vd->width; x++) {
            int r, g, b;
            int y, u, v;
			int o;
			
            if(!z)
                y = yuyv[0] << 8;
            else
                y = yuyv[2] << 8;
            u = yuyv[1] - 128;
            v = yuyv[3] - 128;

            r = (y + (359 * v)) >> 8;
            //g = (y - (88 * u) - (183 * v)) >> 8;
			//b = (y + (454 * u)) >> 8;

            if(i_height >= aoi_y-aoi_range && i_height <= aoi_y+aoi_range &&
				x>=aoi_x-aoi_range && x<=aoi_x+aoi_range) {
					o = (r > 255) ? 255 : ((r < 0) ? 0 : r);
					ratio+=o;
					count++;
			}

            if(z++) {
                z = 0;
                yuyv += 4;
            }
        }
    }

	time_t stop;
	stop = time(NULL);

	counter++;

	//average of circular buffer
	push_to_buffer(ratio/count);
	
	int average = 0;
	int i;
	for (i=0;i<10;i++) {
		average+=circular_buffer[i];
	}
	average/=10;
	

	if(difftime(start,stop) <=-1) {
		fpsX = counter;
		start = 0;
	}

	

	if(count!=0) {
		if (ratio/count - 10 > average && cb_active == 10) {
			//http request

			snprintf(http_req_path, 250, "%s?num=%d&avg=%d", url, (int)(ratio/count), average);
			make_http_request(http_req_path);
			
			
			printf("\033cratio: %d (of %d) OK in %d\n", ratio/count, average, fpsX);
		}else{
			printf("\033cratio: %d (of %d) in %d\n", ratio/count, average, fpsX);
		}
	}

	//start = 0;
}

void start_webcam() {
    /* allocate webcam datastructure */
    videoIn = malloc(sizeof(struct vdIn));
    if(videoIn == NULL) {
        
        exit(EXIT_FAILURE);
    }
    memset(videoIn, 0, sizeof(struct vdIn));

   int id = 4;
    /* open video device and prepare data structure */
	format = V4L2_PIX_FMT_YUYV;
	fps=25;
    if(init_videoIn(videoIn, dev, width, height, fps, format, 1, id) < 0) {
        //IPRINT("init_VideoIn failed\n");
        //closelog();
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char**argv) {
	int c;
	
	opterr = 0;

	while ((c = getopt (argc, argv, "pd:a:u:")) != -1)
		switch (c)
		{
		case 'd':
			//Device name
			dev = strdup(optarg);
			break;

		case 'p':
			//Take picture
			take_pictures = 1;
			break;

		case 'a':
			//Area of interest (XxY,range) - e.g. 150x150,10 -> from 140,140 to 160,160
			area_of_interest = strdup(optarg);
			break;

		case 'u':
			url = strdup(optarg);
			break;
				
		case '?':
			if (isprint (optopt))
				fprintf (stderr, "Unknown option `-%c'.\n", optopt);
			else
				fprintf (stderr,
					"Unknown option character `\\x%x'.\n", optopt);
			return 1;
			
		default:
			abort();
		}

	if (area_of_interest == NULL) {
		printf("Area of interest not set!\n");
		exit(1);
	}else{
		char *string = NULL;
		char *token = NULL;

		if ((token = strsep(&area_of_interest, "x")) != NULL) {
			
			aoi_x = atoi(token);
			if((token = strsep(&area_of_interest, ",")) != NULL) {
				aoi_y = atoi(token);
				
				aoi_range = atoi(area_of_interest);
			}
		}
	}

	if (aoi_x == -1 || aoi_y == -1 || aoi_range == -1) {
		printf("Area of interest not set (2)!\n");
		exit(1);
	}

	if (url == NULL) {
		printf("URL not set!\n");
		exit (1);
	}

	//info message
	printf("Electricity monitoring\n\nDevice: %s\n", dev);
	printf("AOI: %d x %d in range %d\n\n", aoi_x, aoi_y, aoi_range);

	start_webcam();

	if (take_pictures) {
		buf = malloc(videoIn->framesizeIn); //buf for converting yuyv to jpeg
	}
	
	while(1) {
		while(videoIn->streamingState == STREAMING_PAUSED) {
            usleep(1); // maybe not the best way so FIXME
        }

        /* grab a frame */
        if(uvcGrab(videoIn) < 0) {
            //IPRINT("Error grabbing frames\n");
            exit(EXIT_FAILURE);
        }

        
        /*
         * Workaround for broken, corrupted frames:
         * Under low light conditions corrupted frames may get captured.
         * The good thing is such frames are quite small compared to the regular pictures.
         * For example a VGA (640x480) webcam picture is normally >= 8kByte large,
         * corrupted frames are smaller.
         */
       // if(videoIn->buf.bytesused < minimum_size) {
            //DBG("dropping too small frame, assuming it as broken\n");
       //     continue;
       // }
		
        if(videoIn->formatIn == V4L2_PIX_FMT_YUYV) {

			if (take_pictures) {
				//take pictures
				compress_yuyv_to_jpeg(videoIn, buf, videoIn->framesizeIn, 90);

				FILE *handleWrite=fopen("test.jpg","wb");

				/*Writing data to file*/
				fwrite(buf, 1, videoIn->framesizeIn, handleWrite);

				/*Closing File*/
				fclose(handleWrite);
			}
			
			do_my_thing(videoIn);
			
		} else {
		//memcpy_picture(buffer2, videoIn->tmpbuffer, 
			struct jpeg_decompress_struct cinfo;
			struct jpeg_error_mgr jerr;
			/* libjpeg data structure for storing one row, that is, scanline of an image */
			unsigned char *line;
			cinfo.err = jpeg_std_error( &jerr );
			/* setup decompression process and source, then read JPEG header */
			jpeg_create_decompress( &cinfo );
			/* this makes the library read from infile */
			//char* x = (char*)malloc(videoIn->framesizeIn*sizeof(char));
			//memcpy_picture(x, videoIn->tmpbuffer, videoIn->framesizeIn);
			//jpeg_mem_src( &cinfo, videoIn->tmpbuffer, videoIn->framesizeIn);

			FILE *handleWrite=fopen("test.jpg","wb");

     
     fwrite(videoIn->tmpbuffer, 1, videoIn->framesizeIn, handleWrite);

     
     fclose(handleWrite);

			//FILE *handleRead = fmemopen(videoIn->tmpbuffer, videoIn->framesizeIn, "rb");
			FILE *handleRead = fopen("test.jpg", "rb");

			jpeg_stdio_src(&cinfo, handleRead);
			
			
			/* reading the image header which contains image information */
			jpeg_read_header( &cinfo, TRUE );

			width = cinfo.image_width;
		height = cinfo.image_height; 

		/* Start decompression jpeg here */
		jpeg_start_decompress( &cinfo );
			printf("%d\n", width);
while( cinfo.output_scanline < cinfo.output_height )
{
   jpeg_read_scanlines( &cinfo, &line, 1 );
   // for( i=0; i<cinfo.image_width*cinfo.num_components;i++) 
     //  row_pointer[0][i];
}
			

			
			jpeg_finish_decompress( &cinfo );
jpeg_destroy_decompress( &cinfo );
//free( row_pointer[0] );

			//printf("%d\n", width);
           // DBG("copying frame from input: %d\n", (int)pcontext->id);
            //pglobal->in[pcontext->id].size = memcpy_picture(pglobal->in[pcontext->id].buf, pcontext->videoIn->tmpbuffer, pcontext->videoIn->buf.bytesused);
        }

#if 0
        /* motion detection can be done just by comparing the picture size, but it is not very accurate!! */
        if((prev_size - global->size)*(prev_size - global->size) > 4 * 1024 * 1024) {
            DBG("motion detected (delta: %d kB)\n", (prev_size - global->size) / 1024);
        }
        prev_size = global->size;
#endif

        /* copy this frame's timestamp to user space */
        //pglobal->in[pcontext->id].timestamp = pcontext->videoIn->buf.timestamp;

        /* signal fresh_frame */
        //pthread_cond_broadcast(&pglobal->in[pcontext->id].db_update);
        //pthread_mutex_unlock(&pglobal->in[pcontext->id].db);


        /* only use usleep if the fps is below 5, otherwise the overhead is too long */
        if(videoIn->fps < 5) {
            //DBG("waiting for next frame for %d us\n", 1000 * 1000 / pcontext->videoIn->fps);
            usleep(1000 * 1000 / 25);
        } else {
           //DBG("waiting for next frame\n");
        }
    }

  close_v4l2(videoIn);
   // if(videoIn->tmpbuffer != NULL) free(videoIn->tmpbuffer);
   // if(videoIn != NULL) free(pcontext->videoIn);
//}
}
