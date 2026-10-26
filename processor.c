/*
 * processor.c
 *
 * An example image processor for INF1102 project.
 * Reads image from stdin, applies operation, writes to stdout.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "inf1102_config.h"

#define IMAGE_SIZE (INF1102_IMAGE_WIDTH * INF1102_IMAGE_HEIGHT * 3)


static void flip_left_right(uint8_t *im_in, uint8_t *im_out, int width, int height) 
{
    //I'm using a data type of float here, so will need to cast each pixel colour to a float, and then before writing the output will need to cast it back to an int. You will need to lookup in the lab manual which data type you are required to use, based on your INF1102_THRESHOLD_VAL 
    float red_px, green_px, blue_px;
	//loop from top to bottom
	for (int y = 0; y < height; y++) 
	{
		//loop from left to right
        for (int x = 0; x < width; x++) 
        {
            int src_idx = (y * width + x) * 3;  				//input image index
            //the ouput goes from the right to the left (try manually with x=0,1,2,3 and y=0)
            int dst_idx = (y * width + (width - 1 - x)) * 3;	//output image index
            
            //the floating point pixel values are scaled 0.0 to 1.0, but pixels are 0 - 255
            red_px   = (1.0/255.0)*(float)im_in[src_idx];   //R
            green_px = (1.0/255.0)*(float)im_in[src_idx+1]; //G
            blue_px  = (1.0/255.0)*(float)im_in[src_idx+2]; //B
            
            im_out[dst_idx]   = (int)(256*red_px);
            im_out[dst_idx+1] = (int)(256*green_px);
            im_out[dst_idx+2] = (int)(256*blue_px);
        }
    }
}


/* rotate roght */

static void rotate_right(uint8_t *im_in, uint8_t *im_out, int width, int height) {
    double red_px, green_px, blue_px;
    
    for(int y = 0; y < height; y++) {
        for(int x = 0; x < width; x++) {
            int src_idx = (y * width + x) * 3;  
            
            int dst_x = height - 1 - y;
            int dst_y = x;
            
            int dst_idx = (dst_y * height + dst_x) * 3;
            
            red_px = (double)im_in[src_idx];     // R chan
            green_px = (double)im_in[src_idx+1];   // G chan
            blue_px = (double)im_in[src_idx+2];   // B chan
            
            im_out[dst_idx] = (uint8_t)red_px;
            im_out[dst_idx+1] = (uint8_t)green_px;
            im_out[dst_idx+2] = (uint8_t)blue_px;
        }
    }
}


/* ==================== Main ==================== */

int main(void) 
{
    char magic[3];
    int width, height, maxval;
    //read the "magic" value that tells us what kind of image it is, and the size
    if (scanf("%2s %d %d %d", magic, &width, &height, &maxval) != 4) 
    {
        fprintf(stderr, "failed to read PPM header\n");
        return 1;
    }
    getchar(); // consume newline

	//request two memory spaces, one for the input image and one for the output image
    uint8_t *im_in = malloc(width * height * 3);
    uint8_t *im_out = malloc(width * height * 3);
    //end with an error if either memory space can't be allocated
    if (!im_in || !im_out) { perror("malloc"); return 1; }

	//read in the entire image (*3 is for the three colours, R, G & B)
    size_t nread = fread(im_in, 1, width * height * 3, stdin);
    //if the thing that got read in is the wrong size, stop with an error.
    if ((int)nread != width * height * 3) 
    {
        fprintf(stderr, "short read from stdin\n");
        return 1;
    }

	/****************************************************************************/	
	//FOR TESTING, HERE IS AN EXAMPLE ROUTINE THAT FLIPS LEFT-RIGHT
    //REPLACE THIS WITH YOUR CODE THAT DOES WHATEVER FUNCTION YOU ARE SUPPOSED TO DO 
    rotate_right(im_in, im_out, width, height);



	/****************************************************************************/

    fprintf(stderr, "Writing output...\n");
    
    //write the first line of the image (the type, ad the size)
    fprintf(stdout, "P6\n%d %d\n255\n", width, height);
    fflush(stdout);
    
    //write the entire output image
    size_t nw = fwrite(im_out, 1, width * height * 3, stdout);
    if ((int)nw != width * height * 3) 
    {
        fprintf(stderr, "short write to stdout\n");
        return 1;
    }

	//free up the memory we used
    free(im_in);
    free(im_out);
    return 0;
}
