#include <fcntl.h>              //functions like open...
#include <linux/videodev2.h>    //definitions related to V4L2
#include <sys/ioctl.h>          //for ioctl
#include <stdio.h>
//#include <iostream>
#include <string.h>
#include <sys/mman.h>           //for mmap
#include <sys/time.h>           //struct timeval
#include <malloc.h>             //calloc
#include <stdlib.h>             //exit()
#include <assert.h>             //assert()
#include <unistd.h>             //close
#include "SDL/SDL.h"
#include "SdlShow.h"

#define V_WIDTH 640/2
#define V_HEIGHT 480/2

struct buffer{
    void    *start;
    size_t  length;
};

int main(int argc, char *argv[])
{
    int camera_fd;

    //open camera in mode read&write, non-block 
    if( ( camera_fd = open("/dev/video1", O_RDWR|O_NONBLOCK) ) == -1){
        printf( "Open camera device failure\n");
        exit(-1);
    }
    
    //get format information of camera
    struct v4l2_fmtdesc camera_fmt_info;
    memset(&camera_fmt_info, 0, sizeof(struct v4l2_fmtdesc));
    camera_fmt_info.index = 0;
    camera_fmt_info.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    while(ioctl(camera_fd, VIDIOC_ENUM_FMT, &camera_fmt_info) != -1){
        camera_fmt_info.index++;

        printf("Pixelformat:%c%c%c%c\nDescription:%s\n",
                camera_fmt_info.pixelformat & 0xff,
                (camera_fmt_info.pixelformat >> 8) & 0xff,
                (camera_fmt_info.pixelformat >> 16) & 0xff,
                (camera_fmt_info.pixelformat >> 24) & 0xff,
                camera_fmt_info.description);
    }

    //get capability of  camera
    struct v4l2_capability camera_cap_info;
    if(ioctl(camera_fd, VIDIOC_QUERYCAP, &camera_cap_info) != -1){
        printf("get video capability success!\n");
        printf("Driver:%s\nDevice:%s\n",
                camera_cap_info.driver,
                camera_cap_info.card);
    } 
    else{
        printf("Camera do not support ioctl operation\n");
    }

    //set video format
    struct v4l2_format video_fmt_info;
    video_fmt_info.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    video_fmt_info.fmt.pix.width       = V_WIDTH;
    video_fmt_info.fmt.pix.height      = V_HEIGHT;
    video_fmt_info.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    video_fmt_info.fmt.pix.field       = V4L2_FIELD_INTERLACED;
    
    if(ioctl(camera_fd, VIDIOC_S_FMT, &video_fmt_info) != -1){
        printf("Set video format success\n");
    }
   
    /*allocate buffers for video in kernel space, 
     * we will call mmap to map it to the user space later
     */
    struct v4l2_requestbuffers knbuf4video;
    unsigned int buf_count = 4;
    knbuf4video.count  = buf_count;
    knbuf4video.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    knbuf4video.memory = V4L2_MEMORY_MMAP;
    
    if(ioctl(camera_fd, VIDIOC_REQBUFS, &knbuf4video) != -1){
        buf_count = knbuf4video.count;
        printf("Allocate: %d buffers successfully\n",knbuf4video.count);
    }

    //query buffer information
    struct buffer *allocate_bufs;
    unsigned int idx = 0;

    allocate_bufs = (struct buffer *) calloc(buf_count, sizeof(struct buffer));
    
    if(allocate_bufs == NULL){
        printf("allocating buffer fails\n");
        exit(-1);
    }

    /* For each buffer flame, we query it's information, then, map it into user space.
     * We use ioctl with parameter VIDIOC_QUERYBUF to obtain the each buffer's length 
     * and mmap to obtain each buffer's address in userspace. 
     * After all the work done, we must free the memory in user space with mummap
     */
    for(idx = 0; idx < buf_count; idx++){
        struct v4l2_buffer temp_buf;
        memset(&temp_buf, 0, sizeof(temp_buf));
        temp_buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        temp_buf.memory = V4L2_MEMORY_MMAP;
        temp_buf.index  = idx;
        
        //query the no.idx buffer for buffer size and address
        if(ioctl(camera_fd, VIDIOC_QUERYBUF, &temp_buf) == -1){
            printf("ioctl VIDIOC_QUERYBUF fails %d\n", idx);
            exit(-1);
        }
        
        allocate_bufs[idx].length = temp_buf.length;
      
        //map the buffer to the user space
        allocate_bufs[idx].start  =  mmap(NULL, 
                                          temp_buf.length,
                                          PROT_READ|PROT_WRITE,
                                          MAP_SHARED, camera_fd, 
                                          temp_buf.m.offset);
        
        //check for success
        if(allocate_bufs[idx].start == MAP_FAILED){
            printf("mmap faiulre!");
            exit(-1);
        }
    }
    
    /*  Now, we have video date in buffer pointed at by allocate_bufs.
     *  The next step is putting the buffer flames into queue, and, finally,
     *  starting the data stream!
     */
    for(idx = 0; idx < buf_count; idx++){
        struct v4l2_buffer temp_buf;
        memset(&temp_buf, 0, sizeof(temp_buf));
        temp_buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        temp_buf.memory = V4L2_MEMORY_MMAP;
        temp_buf.index  = idx;
        
        //enqueue buffer
        if(ioctl(camera_fd, VIDIOC_QBUF, &temp_buf) == -1){
            printf("ioctl VIDIOC_QBUF fails\n");
            exit(-1);
        }
    }

    //turn on stream, read to read video data!
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    if(ioctl(camera_fd,
            VIDIOC_STREAMON,
            &type) == -1){
            printf("ioctl starting stream  fails\n");
            exit(-1);
    }
    
    // buffer to store video data
    unsigned char *video_buf = (unsigned char *) calloc(307200, sizeof(char));
    FILE *video_file;
    
    if( (video_file = fopen("./videodata1", "ab+") ) == NULL){
        printf("failure: Open file\n");
    }
    else{
        printf("Open file success\n");
    }

    /*In each loop, we judge whether the camera has provide data alreadyly*/
    
    //get video data loop
    printf("Begin reading....\n");

    while(1)
    {
        //we wil create a description set here for the next call select() 
        fd_set fds;
        struct timeval tv;
        int camera_state;

        //initialize the description set and set the bit corresponding to camera_fd to 1
        FD_ZERO(&fds);
        FD_SET(camera_fd, &fds);
        
        //wait for at most 2 seconds
        tv.tv_sec  = 2;
        tv.tv_usec = 0;
        
        /* Tell the kernel we are waitting for the event that file description camera_fd is
         * ready to be read and we will wait at most 2 seconds. Let's say... we are judging 
         * whether the camera is read!
         */
       camera_state =  select(camera_fd+1, &fds, NULL, NULL, &tv);
       
       //handle return state of select
       if( camera_state == -1){
           //do nothing for the time being
           continue;
       }
       else
           if(camera_state == 0){
               printf("select: time out\n");
               continue;
           }
           else;

       /* Dequeue flame*/
       printf("Now copying....\n");
       struct v4l2_buffer temp_buf;
       memset(&temp_buf, 0, sizeof(struct v4l2_buffer));
        
       temp_buf.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE;
       temp_buf.memory  = V4L2_MEMORY_MMAP;
       
       //gather video data into temp_buf
       ioctl(camera_fd, VIDIOC_DQBUF, &temp_buf);
      
       assert(temp_buf.index < buf_count);
        
      //process data
       memcpy(video_buf,
             allocate_bufs[temp_buf.index].start,
             allocate_bufs[temp_buf.index].length);
        
       printf("Copy size %ld\n", sizeof(*video_buf));
       fwrite(allocate_bufs[temp_buf.index].start,
               allocate_bufs[temp_buf.index].length,
               1,
               video_file);
       
       //Set Window
       SdlShow showwin;
       showwin.SdlInitlib(640/2, 480/2);
        
       //Show video
       showwin.SdlWindowsShow(video_buf);

       /* Enqueue the buffer*/
       ioctl(camera_fd, VIDIOC_QBUF, &temp_buf);

    }

    //free the memory in user space
    for(idx = 0; idx < buf_count; idx++){
        if(munmap(allocate_bufs[idx].start,
                allocate_bufs[idx].length)
                == -1){
            printf("mummap error in idx %d", idx);
        }
    }
    
    close(camera_fd);
    fclose(video_file);
    free(video_buf);

    return 0;
}
