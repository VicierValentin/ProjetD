/***************************************************************************
 *   v4l2grab Version 0.1                                                  *
 *   Copyright (C) 2009 by Tobias Müller                                   *
 *   Tobias_Mueller@twam.info                                              *
 *                                                                         *
 *   based on V4L2 Specification, Appendix B: Video Capture Example        *
 *   (http://v4l2spec.bytesex.org/spec/capture-example.html)               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

 #if !defined(IO_READ) && !defined(IO_MMAP) && !defined(IO_USERPTR)
 #error You have to include one of IO_READ, IO_MMAP oder IO_USERPTR!
 #endif
 
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <assert.h>
 #include <getopt.h>
 #include <fcntl.h>
 #include <unistd.h>
 #include <errno.h>
 #include <malloc.h>
 #include <sys/stat.h>
 #include <sys/types.h>
 #include <sys/time.h>
 #include <sys/mman.h>
 #include <sys/ioctl.h>
 #include <asm/types.h>
 #include <linux/videodev2.h>
 #include <jpeglib.h>

 #include "client.h"

 extern char connected;
 
 #define CLEAR(x) memset (&(x), 0, sizeof (x))
 
 typedef enum {
 #ifdef IO_READ
         IO_METHOD_READ,
 #endif
 #ifdef IO_MMAP
         IO_METHOD_MMAP,
 #endif
 #ifdef IO_USERPTR
         IO_METHOD_USERPTR,
 #endif
 } io_method;
 
 struct buffer {
         void *                  start;
         size_t                  length;
 };
 
 static io_method        io              = IO_METHOD_MMAP;
 static int              fd              = -1;
 struct buffer *         buffers         = NULL;
 static unsigned int     n_buffers       = 0;
 
 // global settings
 static unsigned int width = 320;
 static unsigned int height = 240;
 static unsigned char jpegQuality = 60;
 static char* jpegFilename = "image_";
 static char* deviceName = "/dev/video0";

 
 
 /**
   Convert from YUV422 format to RGB888. Formulae are described on http://en.wikipedia.org/wiki/YUV
 
   \param width width of image
   \param height height of image
   \param src source
   \param dst destination
 */
 static void YUV422toRGB888(int width, int height, unsigned char *src, unsigned char *dst)
 {
   int line, column;
   unsigned char *py, *pu, *pv;
   unsigned char *tmp = dst;
 
   /* In this format each four bytes is two pixels. Each four bytes is two Y's, a Cb and a Cr. 
      Each Y goes to one of the pixels, and the Cb and Cr belong to both pixels. */
   py = src;
   pu = src + 1;
   pv = src + 3;
 
   #define CLIP(x) ( (x)>=0xFF ? 0xFF : ( (x) <= 0x00 ? 0x00 : (x) ) )
 
   for (line = 0; line < height; ++line) {
     for (column = 0; column < width; ++column) {
       *tmp++ = CLIP((double)*py + 1.402*((double)*pv-128.0));
       *tmp++ = CLIP((double)*py - 0.344*((double)*pu-128.0) - 0.714*((double)*pv-128.0));      
       *tmp++ = CLIP((double)*py + 1.772*((double)*pu-128.0));
 
       // increase py every time
       py += 2;
       // increase pu,pv every second time
       if ((column & 1)==1) {
         pu += 4;
         pv += 4;
       }
     }
   }
 }
 
 /**
   Print error message and terminate programm with EXIT_FAILURE return code.
   \param s error message to print
 */
 static void errno_exit(const char* s)
 {
   fprintf(stderr, "%s error %d, %s\n", s, errno, strerror (errno));
   exit(EXIT_FAILURE);
 }
 
 /**
   Do ioctl and retry if error was EINTR ("A signal was caught during the ioctl() operation."). Parameters are the same as on ioctl.
 
   \param fd file descriptor
   \param request request
   \param argp argument
   \returns result from ioctl
 */
 static int xioctl(int fd, int request, void* argp)
 {
   int r;
 
   do r = ioctl(fd, request, argp);
   while (-1 == r && EINTR == errno);
 
   return r;
 }
 
 /**
   Write image to jpeg file.
 
   \param img image to write
 */
 static int counterJpeg = 0;

 static void jpegWrite(unsigned char* img)
 {
   struct jpeg_compress_struct cinfo;
   struct jpeg_error_mgr jerr;
   
   JSAMPROW row_pointer[1];
   char buffName[30] = {0};
   snprintf(buffName, sizeof(buffName), "./images/%s%d.jpeg", jpegFilename, counterJpeg++);
   FILE *outfile = fopen( buffName, "wb" );
 
   // try to open file for saving
   if (!outfile) {
     printf("No image to write...");
     return;
   }
 
   // create jpeg data
   cinfo.err = jpeg_std_error( &jerr );
   jpeg_create_compress(&cinfo);
   jpeg_stdio_dest(&cinfo, outfile);
 
   // set image parameters
   cinfo.image_width = width;	
   cinfo.image_height = height;
   cinfo.input_components = 3;
   cinfo.in_color_space = JCS_RGB;
 
   // set jpeg compression parameters to default
   jpeg_set_defaults(&cinfo);
   // and then adjust quality setting
   jpeg_set_quality(&cinfo, jpegQuality, TRUE);
 
   // start compress 
   jpeg_start_compress(&cinfo, TRUE);
 
   // feed data
   while (cinfo.next_scanline < cinfo.image_height) {
     row_pointer[0] = &img[cinfo.next_scanline * cinfo.image_width *  cinfo.input_components];
     jpeg_write_scanlines(&cinfo, row_pointer, 1);
   }
 
   // finish compression
   jpeg_finish_compress(&cinfo);
 
   // destroy jpeg data
   jpeg_destroy_compress(&cinfo);
 
   // close output file
   fclose(outfile);
   send_and_delete_photo(buffName);
 }
 
 /**
   process image read
 */
 static void imageProcess(const void* p)
 {
   unsigned char* src = (unsigned char*)p;
   unsigned char* dst = malloc(width*height*3*sizeof(char));
 
   // convert from YUV422 to RGB888
   YUV422toRGB888(width,height,src,dst);
 
   // write jpeg
   jpegWrite(dst);
 }
 
 /**
   read single frame
 */
 
 static int frameRead(void)
 {
   struct v4l2_buffer buf;
 #ifdef IO_USERPTR
   unsigned int i;
 #endif
 
   switch (io) {
 #ifdef IO_READ
     case IO_METHOD_READ:
       if (-1 == read (fd, buffers[0].start, buffers[0].length)) {
         switch (errno) {
           case EAGAIN:
             return 0;
 
           case EIO:
             // Could ignore EIO, see spec.
 
             // fall through
           default:
             errno_exit("read");
         }
       }
 
       imageProcess(buffers[0].start);
       break;
 #endif
 
 #ifdef IO_MMAP
     case IO_METHOD_MMAP:
       CLEAR (buf);
 
       buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
       buf.memory = V4L2_MEMORY_MMAP;
 
       if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
         switch (errno) {
           case EAGAIN:
             return 0;
 
           case EIO:
              // Could ignore EIO, see spec
 
              // fall through
           default:
             errno_exit("VIDIOC_DQBUF");
         }
       }
 
       assert (buf.index < n_buffers);
 
       imageProcess(buffers[buf.index].start);
 
       if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
         errno_exit("VIDIOC_QBUF");
 
       break;
 #endif
 
 #ifdef IO_USERPTR
     case IO_METHOD_USERPTR:
       CLEAR (buf);
 
       buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
       buf.memory = V4L2_MEMORY_USERPTR;
 
       if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
         switch (errno) {
           case EAGAIN:
             return 0;
 
           case EIO:
             // Could ignore EIO, see spec.
 
             // fall through
           default:
             errno_exit("VIDIOC_DQBUF");
                         
         }
       }
 
       for (i = 0; i < n_buffers; ++i)
         if (buf.m.userptr == (unsigned long) buffers[i].start && buf.length == buffers[i].length)
           break;
 
       assert (i < n_buffers);
 
       imageProcess((void *) buf.m.userptr);
 
       if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
         errno_exit("VIDIOC_QBUF");
       break;
 #endif
 
     }
 
   return 1;
 }
 
 /** 
   mainloop: read frames and process them
 */
 static void mainLoop(void)
 {
   unsigned int count;
 
   count = 1;
 
   while (1) {
     for (;;) {
       fd_set fds;
       struct timeval tv;
       int r;
 
       FD_ZERO(&fds);
       FD_SET(fd, &fds);
 
       /* Timeout. */
       tv.tv_sec = 2;
       tv.tv_usec = 0;
 
       r = select(fd + 1, &fds, NULL, NULL, &tv);
 
       if (-1 == r) {
         if (EINTR == errno)
           continue;
 
         errno_exit("select");
       }
 
       if (0 == r) {
         fprintf (stderr, "select timeout\n");
         exit(EXIT_FAILURE);
       }
 
       if (frameRead())
         break;
         
       /* EAGAIN - continue select loop. */
     }
   }
 }
 
 /**
   stop capturing
 */
 static void captureStop(void)
 {
   enum v4l2_buf_type type;
 
   switch (io) {
 #ifdef IO_READ
     case IO_METHOD_READ:
       /* Nothing to do. */
       break;
 #endif
 
 #ifdef IO_MMAP
     case IO_METHOD_MMAP:
 #endif
 #ifdef IO_USERPTR
     case IO_METHOD_USERPTR:
 #endif
 #if defined(IO_MMAP) || defined(IO_USERPTR)
       type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
 
       if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
         errno_exit("VIDIOC_STREAMOFF");
 
       break;
 #endif 
    }
 }
 
 /**
   start capturing
 */
 static void captureStart(void)
 {
   unsigned int i;
   enum v4l2_buf_type type;
 
   switch (io) {
 #ifdef IO_READ    
     case IO_METHOD_READ:
       /* Nothing to do. */
       break;
 #endif
 
 #ifdef IO_MMAP
     case IO_METHOD_MMAP:
       for (i = 0; i < n_buffers; ++i) {
         struct v4l2_buffer buf;
 
         CLEAR (buf);
 
         buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
         buf.memory      = V4L2_MEMORY_MMAP;
         buf.index       = i;
 
         if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
           errno_exit("VIDIOC_QBUF");
       }
                 
       type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
 
       if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
         errno_exit("VIDIOC_STREAMON");
 
       break;
 #endif
 
 #ifdef IO_USERPTR
     case IO_METHOD_USERPTR:
       for (i = 0; i < n_buffers; ++i) {
         struct v4l2_buffer buf;
 
         CLEAR (buf);
 
         buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
         buf.memory      = V4L2_MEMORY_USERPTR;
         buf.index       = i;
         buf.m.userptr   = (unsigned long) buffers[i].start;
         buf.length      = buffers[i].length;
 
         if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
           errno_exit("VIDIOC_QBUF");
       }
 
       type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
 
       if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
          errno_exit("VIDIOC_STREAMON");
 
       break;
 #endif
   }
 }
 
 static void deviceUninit(void)
 {
   unsigned int i;
 
   switch (io) {
 #ifdef IO_READ
     case IO_METHOD_READ:
       free(buffers[0].start);
       break;
 #endif
 
 #ifdef IO_MMAP
     case IO_METHOD_MMAP:
       for (i = 0; i < n_buffers; ++i)
       if (-1 == munmap (buffers[i].start, buffers[i].length))
         errno_exit("munmap");
       break;
 #endif
 
 #ifdef IO_USERPTR
     case IO_METHOD_USERPTR:
       for (i = 0; i < n_buffers; ++i)
         free (buffers[i].start);
       break;
 #endif
   }
 
   free(buffers);
 }
 
 #ifdef IO_READ
 static void readInit(unsigned int buffer_size)
 {
   buffers = calloc(1, sizeof(*buffers));
 
   if (!buffers) {
     fprintf (stderr, "Out of memory\n");
     exit(EXIT_FAILURE);
   }
 
   buffers[0].length = buffer_size;
   buffers[0].start = malloc (buffer_size);
 
   if (!buffers[0].start) {
     fprintf (stderr, "Out of memory\n");
     exit(EXIT_FAILURE);
   }
 }
 #endif
 
 #ifdef IO_MMAP
 static void mmapInit(void)
 {
   struct v4l2_requestbuffers req;
 
   CLEAR (req);
 
   req.count               = 4;
   req.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
   req.memory              = V4L2_MEMORY_MMAP;
 
   if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
     if (EINVAL == errno) {
       fprintf(stderr, "%s does not support memory mapping\n", deviceName);
       exit(EXIT_FAILURE);
     } else {
       errno_exit("VIDIOC_REQBUFS");
     }
   }
 
   if (req.count < 2) {
     fprintf(stderr, "Insufficient buffer memory on %s\n", deviceName);
     exit(EXIT_FAILURE);
   }
 
   buffers = calloc(req.count, sizeof(*buffers));
 
   if (!buffers) {
     fprintf(stderr, "Out of memory\n");
     exit(EXIT_FAILURE);
   }
 
   for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
     struct v4l2_buffer buf;
 
     CLEAR (buf);
 
     buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
     buf.memory      = V4L2_MEMORY_MMAP;
     buf.index       = n_buffers;
 
     if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
       errno_exit("VIDIOC_QUERYBUF");
 
     buffers[n_buffers].length = buf.length;
     buffers[n_buffers].start =
     mmap (NULL /* start anywhere */, buf.length, PROT_READ | PROT_WRITE /* required */, MAP_SHARED /* recommended */, fd, buf.m.offset);
 
     if (MAP_FAILED == buffers[n_buffers].start)
       errno_exit("mmap");
   }
 }
 #endif
 
 #ifdef IO_USERPTR
 static void userptrInit(unsigned int buffer_size)
 {
   struct v4l2_requestbuffers req;
   unsigned int page_size;
 
   page_size = getpagesize ();
   buffer_size = (buffer_size + page_size - 1) & ~(page_size - 1);
 
   CLEAR(req);
 
   req.count               = 4;
   req.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
   req.memory              = V4L2_MEMORY_USERPTR;
 
   if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
     if (EINVAL == errno) {
       fprintf(stderr, "%s does not support user pointer i/o\n", deviceName);
       exit(EXIT_FAILURE);
     } else {
       errno_exit("VIDIOC_REQBUFS");
     }
   }
 
   buffers = calloc(4, sizeof (*buffers));
 
   if (!buffers) {
     fprintf(stderr, "Out of memory\n");
     exit(EXIT_FAILURE);
   }
 
   for (n_buffers = 0; n_buffers < 4; ++n_buffers) {
     buffers[n_buffers].length = buffer_size;
     buffers[n_buffers].start = memalign (/* boundary */ page_size, buffer_size);
 
     if (!buffers[n_buffers].start) {
       fprintf(stderr, "Out of memory\n");
       exit(EXIT_FAILURE);
     }
   }
 }
 #endif
 
 /**
   initialize device
 */
 static void deviceInit(void)
 {
   struct v4l2_capability cap;
   struct v4l2_cropcap cropcap;
   struct v4l2_crop crop;
   struct v4l2_format fmt;
   unsigned int min;
 
   if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
     if (EINVAL == errno) {
       fprintf(stderr, "%s is no V4L2 device\n",deviceName);
       exit(EXIT_FAILURE);
     } else {
       errno_exit("VIDIOC_QUERYCAP");
     }
   }
 
   if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
     fprintf(stderr, "%s is no video capture device\n",deviceName);
     exit(EXIT_FAILURE);
   }
 
   switch (io) {
 #ifdef IO_READ
     case IO_METHOD_READ:
       if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
         fprintf(stderr, "%s does not support read i/o\n",deviceName);
         exit(EXIT_FAILURE);
       }
       break;
 #endif
 
 #ifdef IO_MMAP
     case IO_METHOD_MMAP:
 #endif
 #ifdef IO_USERPTR
     case IO_METHOD_USERPTR:
 #endif
 #if defined(IO_MMAP) || defined(IO_USERPTR)
       if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
         fprintf(stderr, "%s does not support streaming i/o\n",deviceName);
         exit(EXIT_FAILURE);
       }
       break;
 #endif
   }
 
 
   /* Select video input, video standard and tune here. */
   CLEAR(cropcap);
 
   cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
 
   if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
     crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
     crop.c = cropcap.defrect; /* reset to default */
 
     if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop)) {
       switch (errno) {
         case EINVAL:
           /* Cropping not supported. */
           break;
         default:
           /* Errors ignored. */
           break;
       }
     }
   } else {        
     /* Errors ignored. */
   }
 
   CLEAR (fmt);
 
   // v4l2_format
   fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
   fmt.fmt.pix.width       = width; 
   fmt.fmt.pix.height      = height;
   fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
   fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
 
   if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
     errno_exit("VIDIOC_S_FMT");
 
   /* Note VIDIOC_S_FMT may change width and height. */
   if (width != fmt.fmt.pix.width) {
     width = fmt.fmt.pix.width;
     fprintf(stderr,"Image width set to %i by device %s.\n",width,deviceName);
   }
   if (height != fmt.fmt.pix.height) {
     height = fmt.fmt.pix.height;
     fprintf(stderr,"Image height set to %i by device %s.\n",height,deviceName);
   }
 
   /* Buggy driver paranoia. */
   min = fmt.fmt.pix.width * 2;
   if (fmt.fmt.pix.bytesperline < min)
     fmt.fmt.pix.bytesperline = min;
   min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
   if (fmt.fmt.pix.sizeimage < min)
     fmt.fmt.pix.sizeimage = min;
 
   switch (io) {
 #ifdef IO_READ
     case IO_METHOD_READ:
       readInit(fmt.fmt.pix.sizeimage);
       break;
 #endif
 
 #ifdef IO_MMAP
     case IO_METHOD_MMAP:
       mmapInit();
       break;
 #endif
 
 #ifdef IO_USERPTR
     case IO_METHOD_USERPTR:
       userptrInit(fmt.fmt.pix.sizeimage);
       break;
 #endif
   }
 }
 
 /**
   close device
 */
 static void deviceClose(void)
 {
   if (-1 == close (fd))
     errno_exit("close");
 
   fd = -1;
 }
 
 /**
   open device
 */
 static void deviceOpen(void)
 {
   struct stat st;
 
   // stat file
   if (-1 == stat(deviceName, &st)) {
     fprintf(stderr, "Cannot identify '%s': %d, %s\n", deviceName, errno, strerror (errno));
     exit(EXIT_FAILURE);
   }
 
   // check if its device
   if (!S_ISCHR (st.st_mode)) {
     fprintf(stderr, "%s is no device\n", deviceName);
     exit(EXIT_FAILURE);
   }
 
   // open device
   fd = open(deviceName, O_RDWR /* required */ | O_NONBLOCK, 0);
 
   // check if opening was successfull
   if (-1 == fd) {
     fprintf(stderr, "Cannot open '%s': %d, %s\n", deviceName, errno, strerror (errno));
     exit(EXIT_FAILURE);
   }
 }
 
 int main()
 {
   // open and initialize device
   deviceOpen();
   deviceInit();

   init_client();
 
   // start capturing
   captureStart();
 
   // process frames
   mainLoop();
 
   // stop capturing
   captureStop();
 
   // close device
   deviceUninit();
   deviceClose();
 
   exit(EXIT_SUCCESS);
 
   return 0;
 }