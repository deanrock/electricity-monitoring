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

int take_pictures = 0, aoi_x=-1, aoi_y=-1, aoi_range=-1,
	capture_jpeg_i=0;


int compress_yuyv_to_jpeg(struct vdIn *vd, unsigned char *buffer, int size, int quality);

char *dev = "/dev/video1", *s, *area_of_interest=NULL, *url=NULL;
int width = 320, height = 240, fps = 25, format = V4L2_PIX_FMT_YUYV, i;
struct vdIn *videoIn;
char *buf;

int debug = FALSE;

char http_req_path[250];


//capture FPS
time_t start_capture, stop_capture;
int captured_frames = 0;

//capture to file
void save_to_file(char* b, int size) {
	char name[50];
	snprintf(name, 50, "pictures/%d.jpg", capture_jpeg_i);
	
	FILE *handleWrite=fopen(name, "wb");

	/*Writing data to file*/
	fwrite(b, 1, size, handleWrite);

	/*Closing File*/
	fclose(handleWrite);

	capture_jpeg_i++;
}

//current high

int cur_high = 0;
int cur_high_i = 0;

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
    unsigned char *yuyv;
    int z;
	
    yuyv = vd->framebuffer;
int yu=0;
    z = 0;
    int ratio=0;
    int count=0;
	
	int i_height, i_width;
	//printf("%d, %d\n", vd->height, vd->width);

	/*for (i_height = 0; i_height <= aoi_x-aoi_range; i_height++) {
		for (i_width = 0;i_width < vd->width; i_width++) {
			yu++;

			if (i_height == aoi_x-aoi_range && i_width == aoi_y-aoi_range) {
printf("============================================= %d\n", yu);
			}
		}
	}*/

	

	yu = 0;
	
	
    for(i_height = 0; i_height < vd->height;i_height++) {
        for(i_width = 0; i_width < vd->width; i_width++) {

	//for (i_height = aoi_y-aoi_range; i_height < aoi_y+aoi_range; i_height++ ) {
	//	for (i_width = aoi_x-aoi_range; i_width < aoi_x + aoi_range; i_width++ ) {
			
            
			
            
            //g = (y - (88 * u) - (183 * v)) >> 8;
			//b = (y + (454 * u)) >> 8;

            if(i_height >= aoi_y-aoi_range && i_height <= aoi_y+aoi_range &&
				i_width>=aoi_x-aoi_range && i_width<=aoi_x+aoi_range) {
				//printf("%d %d\n", yu, z);
				//break;
					int r;
				    int y, v;
					int o;
				
					if(!z)
				        y = yuyv[0] << 8;
				    else
				        y = yuyv[2] << 8;
				    //u = yuyv[1] - 128;
				    v = yuyv[3] - 128;

				    r = (y + (359 * v)) >> 8;
				
					o = (r > 255) ? 255 : ((r < 0) ? 0 : r);
					ratio+=o;
					count++;
			}
			
            if(z++) {
                z = 0;
                yuyv += 4;
				yu++;
            }
        }
    }

	//average of circular buffer
	push_to_buffer(ratio/count);
	
	int average = 0;
	int i;
	for (i=0;i<10;i++) {
		average+=circular_buffer[i];
	}
	average/=10;
	
	int red_avg = ratio/count;

	if (cur_high > 0) {
		printf("CH ");
		if (cur_high > red_avg || cur_high_i >= 10) {
			snprintf(http_req_path, 250, "%s?num=%d&avg=%d", url, cur_high, average);
			make_http_request(http_req_path);

			if (debug) printf("ratio: %d (of %d) OK\n", cur_high, average);
			
			cur_high = 0;
			cur_high_i = 0;

			
		}else{
			cur_high = red_avg;
			cur_high_i++;
		}
	}

	if(count!=0) {
		if (red_avg - 10 > average && cb_active == 10) {
			//http request
			
			cur_high = red_avg;	
		}else{
			if (debug) printf("ratio: %d (of %d)\n", red_avg, average);
		}
	}

	//start = 0;
}

void read_config() {
	char* path = "settings.conf";

	FILE* file;

	file = fopen(path, "r");

	if (file == NULL) {
		printf("Cannot open settings.conf\n");
	}else{
		char *line = (char*) malloc(sizeof(char)*255);
		char *token = NULL;
		
		while(fgets(line, 255, file) != NULL) {
			

			if ((token = strsep(&line, "=")) != NULL && line != NULL) {
				if (strlen (token) > 0 && strlen (line) > 0) {
					//find setting

					
					if (line[strlen(line) - 1] == '\n') {
						line[strlen(line) - 1] = '\0';
					}
					
					if (strcmp("area-of-interest", token) == 0) {
						
						area_of_interest = strdup (line);
					}else if (strcmp("url", token) == 0) {
						url = strdup(line);
					}else if (strcmp("device", token) == 0) {
						dev = strdup(line);
					}else if (strcmp("debug", token) == 0) {
						if (strcmp("true", line) == 0) {
							debug = TRUE;
						}
					}
				}
			}
		}

		

		free (line);
		
		//free (token);
		
		
		fclose (file);
	}
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
	
    if(init_videoIn(videoIn, dev, width, height, fps, format, 1, id) < 0) {
        //IPRINT("init_VideoIn failed\n");
        //closelog();
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char**argv) {
	int c;
	
	opterr = 0;

	while ((c = getopt (argc, argv, "djpc:a:u:")) != -1)
		switch (c)
		{
		case 'j':
			//Capture JPEG and save them to disk
			format = V4L2_PIX_FMT_MJPEG;
			break;
		case 'c':
			//Device (cam) name
			dev = strdup(optarg);
			break;

		case 'd':
			debug=TRUE;
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

	read_config();

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
	printf("URL: %s\n", url);
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

		if(start_capture==0){ 
		   start_capture = time(NULL);
		}

		captured_frames++;
		stop_capture = time(NULL);
		if (difftime(start_capture,stop_capture) <=-1) {
			if (debug) printf("captured %d frames in last second\n", captured_frames);
			start_capture = time(NULL);
			captured_frames = 0;
		}
		
        if(videoIn->formatIn == V4L2_PIX_FMT_YUYV) {

			if (take_pictures) {
				//take pictures
				compress_yuyv_to_jpeg(videoIn, buf, videoIn->framesizeIn, 90);

				save_to_file(buf, videoIn->framesizeIn);
			}
			
			do_my_thing(videoIn);
			
		} else if(videoIn->formatIn == V4L2_PIX_FMT_MJPEG) {
			
			//struct jpeg_decompress_struct cinfo;
			//struct jpeg_error_mgr jerr;
			/* libjpeg data structure for storing one row, that is, scanline of an image */
			//unsigned char *line;
			//cinfo.err = jpeg_std_error( &jerr );
			/* setup decompression process and source, then read JPEG header */
			//jpeg_create_decompress( &cinfo );
			/* this makes the library read from infile */
			//char* x = (char*)malloc(videoIn->framesizeIn*sizeof(char));
			//memcpy_picture(x, videoIn->tmpbuffer, videoIn->framesizeIn);
			//jpeg_mem_src( &cinfo, videoIn->tmpbuffer, videoIn->framesizeIn);

			if (take_pictures) {
				//take pictures
				memcpy_picture(buf, videoIn->tmpbuffer, videoIn->framesizeIn);
				save_to_file(buf, videoIn->framesizeIn);
			}

			//FILE *handleRead = fmemopen(videoIn->tmpbuffer, videoIn->framesizeIn, "rb");
			//FILE *handleRead = fopen("test.jpg", "rb");

			//jpeg_stdio_src(&cinfo, handleRead);
			
			
			/* reading the image header which contains image information */
			//jpeg_read_header( &cinfo, TRUE );

			//width = cinfo.image_width;
		//height = cinfo.image_height; 

		/* Start decompression jpeg here
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
//free( row_pointer[0] );*/

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
