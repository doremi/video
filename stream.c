/*
 *  V4L2 video capture example
 *
 *  This program can be used and distributed without restrictions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <getopt.h>             /* getopt_long() */

#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <asm/types.h>          /* for videodev2.h */

#include <linux/videodev2.h>

#include <ft2build.h>
#include <freetype2/freetype/freetype.h>
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <wchar.h>
#include <time.h>

#define START_X 510
#define START_Y 25
#define FONT_SIZE 16

FT_Library library;
FT_Face face;
FT_GlyphSlot slot;
FT_Vector pen;

#define CLEAR(x) memset (&(x), 0, sizeof (x))

typedef enum {
        IO_METHOD_READ,
        IO_METHOD_MMAP,
        IO_METHOD_USERPTR,
} io_method;

struct buffer {
        void *                  start;
        size_t                  length;
};

static char *           dev_name        = NULL;
static io_method        io              = IO_METHOD_MMAP;
static int              fd              = -1;
struct buffer *         buffers         = NULL;
struct buffer *		outbuffers	= NULL;
static unsigned int     n_buffers       = 0;
int global_width, global_height;
unsigned int global_count;
unsigned int global_interval;

struct RGB {
	unsigned char r,g,b;
} __attribute__((packed));

struct YUV {
	unsigned char y1, u, y2, v; //ok in Notebook
//	unsigned char u, y1, v, y2;
} __attribute__((packed));

FILE *out;

struct RGB *image;
struct RGB *image_ptr;

void draw_bitmap( FT_Bitmap*  bitmap,
		  FT_Int      x,
		  FT_Int      y)
{
	FT_Int  i, j, p, q;
	FT_Int  x_max = x + bitmap->width;
	FT_Int  y_max = y + bitmap->rows;


	for ( i = x, p = 0; i < x_max; i++, p++ )
	{
		for ( j = y, q = 0; j < y_max; j++, q++ )
		{
			if ( i >= global_width || j >= global_height )
			  continue;

			image[j*global_width+i].r = bitmap->buffer[q * bitmap->width + p];
			image[j*global_width+i].g = image[j*global_width+i].r;
		}
	}
}

void drawtext(wchar_t *text)
{
	setlocale(LC_CTYPE, "zh_TW.UTF-8");

	FT_Init_FreeType( &library );
	FT_New_Face( library, "courier.ttf", 0, &face );
	FT_Set_Char_Size( face, 0, FONT_SIZE * 64,
			  100, 100 );

	slot = face->glyph;

	pen.x = START_X * 64;
	pen.y = ( global_height - START_Y ) * 64;

	FT_Error error = FT_Select_Charmap( face, FT_ENCODING_UNICODE);
	if ( error != 0 ) {
		printf("select font error");
		perror("select font");
		exit(1);
	}

	int num_chars, n;
	num_chars = wcslen( text );

	for ( n = 0; n < num_chars; ++n )
	{
		FT_Set_Transform( face, NULL, &pen );
		FT_Load_Char( face, text[n], FT_LOAD_RENDER );

		draw_bitmap( &slot->bitmap,
			     slot->bitmap_left,
			     global_height - slot->bitmap_top );

		pen.x += slot->advance.x;
		pen.y += slot->advance.y;
	}

	// move to new line
	pen.y -= FONT_SIZE *2 * 64;
	pen.x  = 50 * 64;


	FT_Done_Face(face);
	FT_Done_FreeType(library);
}

inline void my_fwrite(const void *ptr)
{
	*image_ptr++ = *(struct RGB*)ptr;
}

inline void my_fflush(void *ptr)
{
	wchar_t text[80];
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	swprintf(text, sizeof(text)/sizeof(wchar_t), L"Fuck @ %d/%d %02d:%02d:%02d",
		tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	drawtext(text);
	fwrite((void*)image, global_width*global_height*3, 1, out);
	fflush(ptr);
}

void yuv2rgb(int index)
{
	struct RGB rgb;
	struct YUV yuv;

	int r,g,b;
	int count = 0;

	if (!out)
		exit(1);

	image_ptr = image;
	do {
		memcpy((void*)&yuv, buffers[index].start + count, sizeof(struct YUV));
		count += sizeof(struct YUV);

		r = yuv.y1 + (1.4075*(yuv.v-128));
		g = yuv.y1 - (0.3455*(yuv.u-128)-(0.7169*(yuv.v-128)));
		b = yuv.y1 + (1.7790*(yuv.u-128));
		if (r > 255) r = 255;
		if (g > 255) g = 255;
		if (b > 255) b = 255;
		if (r < 0) r = 0;
		if (g < 0) g = 0;
		if (b < 0) b = 0;
		rgb.r = r;
		rgb.g = g;
		rgb.b = b;
		my_fwrite((void*)&rgb);

                r = yuv.y2 + (1.4075*(yuv.v-128));
                g = yuv.y2 - (0.3455*(yuv.u-128)-(0.7169*(yuv.v-128)));
                b = yuv.y2 + (1.7790*(yuv.u-128));
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
                if (r < 0) r = 0;
                if (g < 0) g = 0;
                if (b < 0) b = 0;
		rgb.r = r;
		rgb.g = g;
		rgb.b = b;
		my_fwrite((void*)&rgb);
	} while (count < global_width*global_height*2);
	my_fflush(NULL);
}

static void
errno_exit                      (const char *           s)
{
        fprintf (stderr, "%s error %d, %s\n",
                 s, errno, strerror (errno));

        exit (EXIT_FAILURE);
}

static int
xioctl                          (int                    fd,
                                 int                    request,
                                 void *                 arg)
{
        int r;

        do r = ioctl (fd, request, arg);
        while (-1 == r && EINTR == errno);

        return r;
}

static void
process_image                   (const void *           p, int index)
{
	static unsigned int i = 0;
	printf("Process image [%d/%d]...\r", i++, global_count);
	fflush(NULL);
	yuv2rgb(index);
}

static int
read_frame                      (void)
{
        struct v4l2_buffer buf;
        unsigned int i;

        switch (io) {
        case IO_METHOD_READ:
                if (-1 == read (fd, buffers[0].start, buffers[0].length)) {
                        switch (errno) {
                        case EAGAIN:
                                return 0;

                        case EIO:
                                /* Could ignore EIO, see spec. */

                                /* fall through */

                        default:
                                errno_exit ("read");
                        }
                }

                process_image (buffers[0].start, 0);

                break;

        case IO_METHOD_MMAP:
                CLEAR (buf);

                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;

                if (-1 == xioctl (fd, VIDIOC_DQBUF, &buf)) {
			printf("DQBUF error!!\n");
                        switch (errno) {
                        case EAGAIN:
				printf("DQBUF EAGAIN\n");
                                return 0;

                        case EIO:
				printf("DQBUF EIO\n");
                                /* Could ignore EIO, see spec. */

                                /* fall through */

                        default:
                                errno_exit ("VIDIOC_DQBUF");
                        }
                }
//		printf("DQBUF ok! I get [%d] buffer\n", buf.index);

                assert (buf.index < n_buffers);

                process_image (buffers[buf.index].start, buf.index);

                if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
                        errno_exit ("VIDIOC_QBUF");

                break;

        case IO_METHOD_USERPTR:
                CLEAR (buf);

                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_USERPTR;

                if (-1 == xioctl (fd, VIDIOC_DQBUF, &buf)) {
                        switch (errno) {
                        case EAGAIN:
                                return 0;

                        case EIO:
                                /* Could ignore EIO, see spec. */

                                /* fall through */

                        default:
                                errno_exit ("VIDIOC_DQBUF");
                        }
                }

                for (i = 0; i < n_buffers; ++i)
                        if (buf.m.userptr == (unsigned long) buffers[i].start
                            && buf.length == buffers[i].length)
                                break;

                assert (i < n_buffers);

                process_image ((void *) buf.m.userptr, 0);

                if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
                        errno_exit ("VIDIOC_QBUF");

                break;
        }

        return 1;
}

static void
mainloop                        (void)
{
        unsigned int count;

        count = global_count;

        while (count-- > 0) {
                for (;;) {
                        if (read_frame ())
                                break;
                }
		sleep(global_interval);
	}
}

static void
stop_capturing                  (void)
{
        enum v4l2_buf_type type;

        switch (io) {
        case IO_METHOD_READ:
                /* Nothing to do. */
                break;

        case IO_METHOD_MMAP:
        case IO_METHOD_USERPTR:
                type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

                if (-1 == xioctl (fd, VIDIOC_STREAMOFF, &type))
                        errno_exit ("VIDIOC_STREAMOFF");

                break;
        }
}

static void
start_capturing                 (void)
{
        unsigned int i;
        enum v4l2_buf_type type;

        switch (io) {
        case IO_METHOD_READ:
		printf("Using read\n");
                /* Nothing to do. */
                break;

        case IO_METHOD_MMAP:
                for (i = 0; i < n_buffers; ++i) {
                        struct v4l2_buffer buf;

                        CLEAR (buf);

                        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                        buf.memory      = V4L2_MEMORY_MMAP;
                        buf.index       = i;

                        if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
                                errno_exit ("VIDIOC_QBUF");
                }
                printf("I queued %d buffers!\n", n_buffers);
                type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		printf("STREAM ON...\n");
                if (-1 == xioctl (fd, VIDIOC_STREAMON, &type))
                        errno_exit ("VIDIOC_STREAMON");
		printf("STREAM ON OK!\n");

                break;

        case IO_METHOD_USERPTR:
                for (i = 0; i < n_buffers; ++i) {
                        struct v4l2_buffer buf;

                        CLEAR (buf);

                        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                        buf.memory      = V4L2_MEMORY_USERPTR;
                        buf.index       = i;
                        buf.m.userptr   = (unsigned long) buffers[i].start;
                        buf.length      = buffers[i].length;

                        if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
                                errno_exit ("VIDIOC_QBUF");
                }

                type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

                if (-1 == xioctl (fd, VIDIOC_STREAMON, &type))
                        errno_exit ("VIDIOC_STREAMON");

                break;
        }
}

static void
uninit_device                   (void)
{
        unsigned int i;

        switch (io) {
        case IO_METHOD_READ:
                free (buffers[0].start);
                break;

        case IO_METHOD_MMAP:
                for (i = 0; i < n_buffers; ++i)
                        if (-1 == munmap (buffers[i].start, buffers[i].length))
                                errno_exit ("munmap");
                break;

        case IO_METHOD_USERPTR:
                for (i = 0; i < n_buffers; ++i)
                        free (buffers[i].start);
                break;
        }

        free (buffers);
	fclose(out);
}

static void
init_read                       (unsigned int           buffer_size)
{
        buffers = calloc (1, sizeof (*buffers));

        if (!buffers) {
                fprintf (stderr, "Out of memory\n");
                exit (EXIT_FAILURE);
        }

        buffers[0].length = buffer_size;
        buffers[0].start = malloc (buffer_size);

        if (!buffers[0].start) {
                fprintf (stderr, "Out of memory\n");
                exit (EXIT_FAILURE);
        }
}

static void
init_mmap                       (void)
{
        struct v4l2_requestbuffers req;

        CLEAR (req);

        req.count               = 4;
        req.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory              = V4L2_MEMORY_MMAP;

        if (-1 == xioctl (fd, VIDIOC_REQBUFS, &req)) {
                if (EINVAL == errno) {
                        fprintf (stderr, "%s does not support "
                                 "memory mapping\n", dev_name);
                        exit (EXIT_FAILURE);
                } else {
                        errno_exit ("VIDIOC_REQBUFS");
                }
        }


        if (req.count < 2) {
                fprintf (stderr, "Insufficient buffer memory on %s\n",
                         dev_name);
                exit (EXIT_FAILURE);
        }

        buffers = calloc (req.count, sizeof (*buffers));
	outbuffers = calloc (req.count, sizeof (*outbuffers));

        if (!buffers) {
                fprintf (stderr, "Out of memory\n");
                exit (EXIT_FAILURE);
        }

        for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
                struct v4l2_buffer buf;

                CLEAR (buf);

                buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory      = V4L2_MEMORY_MMAP;
                buf.index       = n_buffers;

                if (-1 == xioctl (fd, VIDIOC_QUERYBUF, &buf))
                        errno_exit ("VIDIOC_QUERYBUF");

                buffers[n_buffers].length = buf.length;
                buffers[n_buffers].start =
                        mmap (NULL /* start anywhere */,
                              buf.length,
                              PROT_READ | PROT_WRITE /* required */,
                              MAP_SHARED /* recommended */,
                              fd, buf.m.offset);

                if (MAP_FAILED == buffers[n_buffers].start)
                        errno_exit ("mmap");
        }
}

static void
init_userp                      (unsigned int           buffer_size)
{
        struct v4l2_requestbuffers req;
        unsigned int page_size;

        page_size = getpagesize ();
        buffer_size = (buffer_size + page_size - 1) & ~(page_size - 1);

        CLEAR (req);

        req.count               = 4;
        req.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory              = V4L2_MEMORY_USERPTR;

        if (-1 == xioctl (fd, VIDIOC_REQBUFS, &req)) {
                if (EINVAL == errno) {
                        fprintf (stderr, "%s does not support "
                                 "user pointer i/o\n", dev_name);
                        exit (EXIT_FAILURE);
                } else {
                        errno_exit ("VIDIOC_REQBUFS");
                }
        }

        buffers = calloc (4, sizeof (*buffers));

        if (!buffers) {
                fprintf (stderr, "Out of memory\n");
                exit (EXIT_FAILURE);
        }

        for (n_buffers = 0; n_buffers < 4; ++n_buffers) {
                buffers[n_buffers].length = buffer_size;
                buffers[n_buffers].start = memalign (/* boundary */ page_size,
                                                     buffer_size);

                if (!buffers[n_buffers].start) {
                        fprintf (stderr, "Out of memory\n");
                        exit (EXIT_FAILURE);
                }
        }
}

static void
init_device                     (int x, int y)
{
        struct v4l2_capability cap;
        struct v4l2_cropcap cropcap;
        struct v4l2_crop crop;
        struct v4l2_format fmt;
        unsigned int min;

        if (-1 == xioctl (fd, VIDIOC_QUERYCAP, &cap)) {
                if (EINVAL == errno) {
                        fprintf (stderr, "%s is no V4L2 device\n",
                                 dev_name);
                        exit (EXIT_FAILURE);
                } else {
                        errno_exit ("VIDIOC_QUERYCAP");
                }
        }

        if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
                fprintf (stderr, "%s is no video capture device\n",
                         dev_name);
                exit (EXIT_FAILURE);
        }

        switch (io) {
        case IO_METHOD_READ:
                if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
                        fprintf (stderr, "%s does not support read i/o\n",
                                 dev_name);
                        exit (EXIT_FAILURE);
                }

                break;

        case IO_METHOD_MMAP:
        case IO_METHOD_USERPTR:
                if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
                        fprintf (stderr, "%s does not support streaming i/o\n",
                                 dev_name);
                        exit (EXIT_FAILURE);
                }

                break;
        }


        /* Select video input, video standard and tune here. */


        CLEAR (cropcap);

        cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (0 == xioctl (fd, VIDIOC_CROPCAP, &cropcap)) {
                crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                crop.c = cropcap.defrect; /* reset to default */

                if (-1 == xioctl (fd, VIDIOC_S_CROP, &crop)) {
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

        fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width       = x; //original: 720x576
        fmt.fmt.pix.height      = y;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
//        fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;

	if (fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV)
		printf("Output is YUV422\n");
	else
		printf("Output is YUV420\n");

        if (-1 == xioctl (fd, VIDIOC_S_FMT, &fmt))
                errno_exit ("VIDIOC_S_FMT");

        /* Note VIDIOC_S_FMT may change width and height. */

        /* Buggy driver paranoia. */
        min = fmt.fmt.pix.width * 2;
        if (fmt.fmt.pix.bytesperline < min)
                fmt.fmt.pix.bytesperline = min;
        min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
        if (fmt.fmt.pix.sizeimage < min)
                fmt.fmt.pix.sizeimage = min;

        switch (io) {
        case IO_METHOD_READ:
                init_read (fmt.fmt.pix.sizeimage);
                break;

        case IO_METHOD_MMAP:
                init_mmap ();
                break;

        case IO_METHOD_USERPTR:
                init_userp (fmt.fmt.pix.sizeimage);
                break;
        }
}

static void
close_device                    (void)
{
        if (-1 == close (fd))
                errno_exit ("close");

        fd = -1;
}

static void
open_device                     (void)
{
        struct stat st; 

        if (-1 == stat (dev_name, &st)) {
                fprintf (stderr, "Cannot identify '%s': %d, %s\n",
                         dev_name, errno, strerror (errno));
                exit (EXIT_FAILURE);
        }

        if (!S_ISCHR (st.st_mode)) {
                fprintf (stderr, "%s is no device\n", dev_name);
                exit (EXIT_FAILURE);
        }

        fd = open (dev_name, O_RDWR /* required */ , 0);

        if (-1 == fd) {
                fprintf (stderr, "Cannot open '%s': %d, %s\n",
                         dev_name, errno, strerror (errno));
                exit (EXIT_FAILURE);
        }

/*
	struct v4l2_format fmt;
	memset((void*)&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = 800;
	fmt.fmt.pix.height = 600;
	ioctl(fd, VIDIOC_S_FMT, &fmt);
	printf("X: %d, Y: %d\n", fmt.fmt.pix.width, fmt.fmt.pix.height);
*/
}

void init_image()
{
	image = malloc(global_width*global_height*sizeof(struct RGB));
	if (!image)
		perror("malloc");
}

void free_image()
{
	free(image);
}

static void
usage                           (FILE *                 fp,
                                 int                    argc,
                                 char **                argv)
{
        fprintf (fp,
                 "Usage: %s [options]\n\n"
                 "Options:\n"
                 "-d | --device name   Video device name [/dev/video]\n"
                 "-h | --help          Print this message\n"
                 "-m | --mmap          Use memory mapped buffers\n"
                 "-r | --read          Use read() calls\n"
                 "-u | --userp         Use application allocated buffers\n"
                 "-c | --count         How many frame will be captured\n"
                 "-i | --interval      The interval of one frame between the second frame\n"
                 "",
                 argv[0]);
}

static const char short_options [] = "d:hmrux:y:c:i:";

static const struct option
long_options [] = {
        { "device",     required_argument,      NULL,           'd' },
        { "help",       no_argument,            NULL,           'h' },
        { "mmap",       no_argument,            NULL,           'm' },
        { "read",       no_argument,            NULL,           'r' },
        { "userp",      no_argument,            NULL,           'u' },
	{ "x",          required_argument,      NULL,           'x' },
	{ "y",          required_argument,      NULL,           'y' },
	{ "count",	required_argument,	NULL,		'c' },
	{ "interval",	required_argument,	NULL,		'i' },
        { 0, 0, 0, 0 }
};

int
main                            (int                    argc,
                                 char **                argv)
{
        dev_name = "/dev/video0";
	int width = 800;
	int height = 600;

	out = fopen("out2.raw", "wb");
        for (;;) {
                int index;
                int c;
                
                c = getopt_long (argc, argv,
                                 short_options, long_options,
                                 &index);

                if (-1 == c)
                        break;

                switch (c) {
                case 0: /* getopt_long() flag */
                        break;

                case 'd':
                        dev_name = optarg;
                        break;

                case 'h':
                        usage (stdout, argc, argv);
                        exit (EXIT_SUCCESS);

                case 'm':
                        io = IO_METHOD_MMAP;
                        break;

                case 'r':
                        io = IO_METHOD_READ;
                        break;

                case 'u':
                        io = IO_METHOD_USERPTR;
                        break;

		case 'x':
			width = atoi(optarg);
			break;
		case 'y':
			height = atoi(optarg);
			break;
		case 'c':
			global_count = atoi(optarg);
			break;
		case 'i':
			global_interval = atoi(optarg);
			break;
                default:
                        usage (stderr, argc, argv);
                        exit (EXIT_FAILURE);
                }
        }

	global_width = width;
	global_height = height;

	open_device ();

	init_image();

        init_device (width, height);

        start_capturing ();

        mainloop ();

        stop_capturing ();

        uninit_device ();

        close_device ();

	free_image();

        exit (EXIT_SUCCESS);

	printf("\n");

        return 0;
}
