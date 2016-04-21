/*
 * sprites.c
 * program which demonstrates GBA sprites
 */

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 160

/* include the background image we are using */
#include "background.h"

/* include the sprite image we are using */
#include "ship.h"
#include "laser.h"

/* include the tile map we are using */
#include "map.h"
#include "map2.h"

/* the tile mode flags needed for display control register */
#define  MODE0 0x00
#define BG0_ENABLE 0x100
#define BG1_ENABLE 0x200

/* flags to set sprite handling in display control register */
#define SPRITE_MAP_2D 0x0
#define SPRITE_MAP_1D 0x40
#define SPRITE_ENABLE 0x1000


/* the control registers for the four tile layers */
volatile unsigned short* bg0_control = (volatile unsigned short*) 0x4000008;

/* palette is always 256 colors */
#define PALETTE_SIZE 256

/* there are 128 sprites on the GBA */
#define NUM_SPRITES 128

/* the display control pointer points to the gba graphics register */
volatile unsigned long* display_control = (volatile unsigned long*) 0x4000000;

/* the memory location which controls sprite attributes */
volatile unsigned short* sprite_attribute_memory = (volatile unsigned short*) 0x7000000;

/* the memory location which stores sprite image data */
volatile unsigned short* sprite_image_memory = (volatile unsigned short*) 0x6010000;

/* the address of the color palettes used for backgrounds and sprites */
volatile unsigned short* bg_palette = (volatile unsigned short*) 0x5000000;
volatile unsigned short* sprite_palette = (volatile unsigned short*) 0x5000200;

/* the button register holds the bits which indicate whether each button has
 * been pressed - this has got to be volatile as well
 */
volatile unsigned short* buttons = (volatile unsigned short*) 0x04000130;

/* scrolling registers for backgrounds */
volatile short* bg0_x_scroll = (unsigned short*) 0x4000010;
volatile short* bg0_y_scroll = (unsigned short*) 0x4000012;

/* the bit positions indicate each button - the first bit is for A, second for
 * B, and so on, each constant below can be ANDED into the register to get the
 * status of any one button */
#define BUTTON_A (1 << 0)
#define BUTTON_B (1 << 1)
#define BUTTON_SELECT (1 << 2)
#define BUTTON_START (1 << 3)
#define BUTTON_RIGHT (1 << 4)
#define BUTTON_LEFT (1 << 5)
#define BUTTON_UP (1 << 6)
#define BUTTON_DOWN (1 << 7)
#define BUTTON_R (1 << 8)
#define BUTTON_L (1 << 9)

/* the scanline counter is a memory cell which is updated to indicate how
 * much of the screen has been drawn */
volatile unsigned short* scanline_counter = (volatile unsigned short*) 0x4000006;

/* wait for the screen to be fully drawn so we can do something during vblank */
void wait_vblank( ) {
    /* wait until all 160 lines have been updated */
    while (*scanline_counter < 160) { }
}

/* this function checks whether a particular button has been pressed */
unsigned char button_pressed(unsigned short button) {
    /* and the button register with the button constant we want */
    unsigned short pressed = *buttons & button;

    /* if this value is zero, then it's not pressed */
    if (pressed == 0) {
        return 1;
    } else {
        return 0;
    }
}

/* return a pointer to one of the 4 character blocks (0-3) */
volatile unsigned short* char_block(unsigned long block) {
    /* they are each 16K big */
    return (volatile unsigned short*) (0x6000000 + (block * 0x4000));
}

/* return a pointer to one of the 32 screen blocks (0-31) */
volatile unsigned short* screen_block(unsigned long block) {
    /* they are each 2K big */
    return (volatile unsigned short*) (0x6000000 + (block * 0x800));
}

/* flag for turning on DMA */
#define DMA_ENABLE 0x80000000

/* flags for the sizes to transfer, 16 or 32 bits */
#define DMA_16 0x00000000
#define DMA_32 0x04000000

/* pointer to the DMA source location */
volatile unsigned int* dma_source = (volatile unsigned int*) 0x40000D4;

/* pointer to the DMA destination location */
volatile unsigned int* dma_destination = (volatile unsigned int*) 0x40000D8;

/* pointer to the DMA count/control */
volatile unsigned int* dma_count = (volatile unsigned int*) 0x40000DC;

/* copy data using DMA */
void memcpy16_dma(unsigned short* dest, unsigned short* source, int amount) {
    *dma_source = (unsigned int) source;
    *dma_destination = (unsigned int) dest;
    *dma_count = amount | DMA_16 | DMA_ENABLE;
}

/* function to setup background 0 for this program */
void setup_background() {

    /* load the palette from the image into palette memory*/
    memcpy16_dma((unsigned short*) bg_palette, (unsigned short*) background_palette, PALETTE_SIZE);

    /* load the image into char block 0 */
    memcpy16_dma((unsigned short*) char_block(0), (unsigned short*) background_data,
            (background_width * background_height) / 2);
/*
    // load the palette from background 2
    memcpy16_dma((unsigned short*) bg2_palette, (unsigned short*) background2_palette, PALETTE_SIZE);

    // load the image into char block 1
    memcpy16_dma((unsigned short*) char_block(0), (unsigned short*) background_data,
            (background_width * background_height) / 2);
*/

    /* set all control the bits in this register */
    *bg0_control = 0 |    /* priority, 0 is highest, 3 is lowest */
        (0 << 2)  |       /* the char block the image data is stored in */
        (0 << 6)  |       /* the mosaic flag */
        (1 << 7)  |       /* color mode, 0 is 16 colors, 1 is 256 colors */
        (16 << 8) |       /* the screen block the tile data is stored in */
        (1 << 13) |       /* wrapping flag */
        (0 << 14);        /* bg size, 0 is 256x256 */
/*
	//set control bits in bg1 register
	*bg1_control = 0 |
        (0 << 2)  |       
        (0 << 6)  |      
        (1 << 7)  |       
        (16 << 8) |       
        (1 << 13) |       
        (0 << 14);        
*/
    /* load the tile data into screen block 16 */
    memcpy16_dma((unsigned short*) screen_block(16), (unsigned short*) map, map_width * map_height);
}

/* just kill time */
void delay(unsigned int amount) {
    for (int i = 0; i < amount * 10; i++);
}

/* a sprite is a moveable image on the screen */
struct Sprite {
    unsigned short attribute0;
    unsigned short attribute1;
    unsigned short attribute2;
    unsigned short attribute3;
};

/* array of all the sprites available on the GBA */
struct Sprite sprites[NUM_SPRITES];
int next_sprite_index = 0;

/* the different sizes of sprites which are possible */
enum SpriteSize {
    SIZE_8_8,
    SIZE_16_16,
    SIZE_32_32,
    SIZE_64_64,
    SIZE_16_8,
    SIZE_32_8,
    SIZE_32_16,
    SIZE_64_32,
    SIZE_8_16,
    SIZE_8_32,
    SIZE_16_32,
    SIZE_32_64
};

/* function to initialize a sprite with its properties, and return a pointer */
struct Sprite* sprite_init(int x, int y, enum SpriteSize size,
        int horizontal_flip, int vertical_flip, int tile_index, int priority) {

    /* grab the next index */
    int index = next_sprite_index++;

    /* setup the bits used for each shape/size possible */
    int size_bits, shape_bits;
    switch (size) {
        case SIZE_8_8:   size_bits = 0; shape_bits = 0; break;
        case SIZE_16_16: size_bits = 1; shape_bits = 0; break;
        case SIZE_32_32: size_bits = 2; shape_bits = 0; break;
        case SIZE_64_64: size_bits = 3; shape_bits = 0; break;
        case SIZE_16_8:  size_bits = 0; shape_bits = 1; break;
        case SIZE_32_8:  size_bits = 1; shape_bits = 1; break;
        case SIZE_32_16: size_bits = 2; shape_bits = 1; break;
        case SIZE_64_32: size_bits = 3; shape_bits = 1; break;
        case SIZE_8_16:  size_bits = 0; shape_bits = 2; break;
        case SIZE_8_32:  size_bits = 1; shape_bits = 2; break;
        case SIZE_16_32: size_bits = 2; shape_bits = 2; break;
        case SIZE_32_64: size_bits = 3; shape_bits = 2; break;
    }

    int h = horizontal_flip ? 1 : 0;
    int v = vertical_flip ? 1 : 0;

    /* set up the first attribute */
    sprites[index].attribute0 = y |             /* y coordinate */
                            (0 << 8) |          /* rendering mode */
                            (0 << 10) |         /* gfx mode */
                            (0 << 12) |         /* mosaic */
                            (1 << 13) |         /* color mode, 0:16, 1:256 */
                            (shape_bits << 14); /* shape */

    /* set up the second attribute */
    sprites[index].attribute1 = x |             /* x coordinate */
                            (0 << 9) |          /* affine flag */
                            (h << 12) |         /* horizontal flip flag */
                            (v << 13) |         /* vertical flip flag */
                            (size_bits << 14);  /* size */

    /* setup the second attribute */
    sprites[index].attribute2 = tile_index |   // tile index */
                            (priority << 10) | // priority */
                            (0 << 12);         // palette bank (only 16 color)*/

    /* return pointer to this sprite */
    return &sprites[index];
}

/* update all of the spries on the screen */
void sprite_update_all() {
    /* copy them all over */
    memcpy16_dma((unsigned short*) sprite_attribute_memory, (unsigned short*) sprites, NUM_SPRITES * 4);
}

/* setup all sprites */
void sprite_clear() {
    /* clear the index counter */
    next_sprite_index = 0;

    /* move all sprites offscreen to hide them */
    for(int i = 0; i < NUM_SPRITES; i++) {
        sprites[i].attribute0 = SCREEN_HEIGHT;
        sprites[i].attribute1 = SCREEN_WIDTH;
    }
}

/* set a sprite postion */
void sprite_position(struct Sprite* sprite, int x, int y) {
    /* clear out the y coordinate */
    sprite->attribute0 &= 0xff00;

    /* set the new y coordinate */
    sprite->attribute0 |= (y & 0xff);

    /* clear out the x coordinate */
    sprite->attribute1 &= 0xfe00;

    /* set the new x coordinate */
    sprite->attribute1 |= (x & 0x1ff);
}

/* move a sprite in a direction */
void sprite_move(struct Sprite* sprite, int dx, int dy) {
    /* get the current y coordinate */
    int y = sprite->attribute0 & 0xff;

    /* get the current x coordinate */
    int x = sprite->attribute1 & 0x1ff;

    /* move to the new location */
    sprite_position(sprite, x + dx, y + dy);
}

/* change the vertical flip flag */
void sprite_set_vertical_flip(struct Sprite* sprite, int vertical_flip) {
    if (vertical_flip) {
        /* set the bit */
        sprite->attribute1 |= 0x2000;
    } else {
        /* clear the bit */
        sprite->attribute1 &= 0xdfff;
    }
}

/* change the vertical flip flag */
void sprite_set_horizontal_flip(struct Sprite* sprite, int horizontal_flip) {
    if (horizontal_flip) {
        /* set the bit */
        sprite->attribute1 |= 0x1000;
    } else {
        /* clear the bit */
        sprite->attribute1 &= 0xefff;
    }
}

/* change the tile offset of a sprite */
void sprite_set_offset(struct Sprite* sprite, int offset) {
    /* clear the old offset */
    sprite->attribute2 &= 0xfc00;

    /* apply the new one */
    sprite->attribute2 |= (offset & 0x03ff);
}

/* setup the sprite image and palette */
void setup_sprite_image() {
    /* load the palette from the image into palette memory*/
    memcpy16_dma((unsigned short*) sprite_palette, (unsigned short*) ship_palette, PALETTE_SIZE);

    /* load the image into char block 0 */
    memcpy16_dma((unsigned short*) sprite_image_memory, (unsigned short*) ship_data, (ship_width * ship_height) / 2);
}

/* a struct for the ship's logic and behavior */
struct Ship {
    /* the actual sprite attribute info */
    struct Sprite* sprite;

    /* the x and y postion */
    int x, y;

    /* which frame of the animation he is on */
    int frame;

    /* the number of frames to wait before flipping */
    int animation_delay;

    /* the animation counter counts how many frames until we flip */
    int counter;

    /* whether the ship is moving right now or not */
    int move;

    /* the number of pixels away from the edge of the screen the ship stays */
    int border;
};

struct Laser {
	struct Sprite* sprite;
	int x, y;
	int frame;
	int animation_delay;
	int counter;
	int move;
	int border;
};

/* initialize the ship */
void ship_init(struct Ship* ship) {
    ship->x = 100;
    ship->y = 140;
    ship->border = 40;
    ship->frame = 0;
    ship->move = 0;
    ship->counter = 0;
    ship->animation_delay = 8;
    ship->sprite = sprite_init(ship->x, ship->y, SIZE_16_16, 0, 0, ship->frame, 0);
}

void laser_init(struct Laser* laser) {
	laser->x = 200;
	laser->y = 200;
	laser->border = 40;
	laser->frame = 8;
	laser->move = 0;
	laser->counter = 0;
	laser->animation_delay = 8;
	laser->sprite = sprite_init(laser->x, laser->y, SIZE_16_16, 0, 0, laser->frame, 0);
}

/* move the ship left or right returns if it is at edge of the screen */
int ship_left(struct Ship* ship) {
    /* face left */
//    sprite_set_horizontal_flip(ship->sprite, 1);
    ship->move = 1;

    /* if we are at the left end, just scroll the screen */
    if (ship->x < ship->border) {
        return 1;
    } else {
        /* else move left */
        ship->x--;
        return 0;
    }
}
int ship_right(struct Ship* ship) {
    /* face right */
    sprite_set_horizontal_flip(ship->sprite, 0);
    ship->move = 1;

    /* if we are at the right end, just scroll the screen */
    if (ship->x > (SCREEN_WIDTH - 16 - ship->border)) {
        return 1;
    } else {
        /* else move right */
        ship->x++;
        return 0;
    }
}
//returns if the ship is at the top of it's allowed range
int ship_top(struct Ship* ship) {
	sprite_set_vertical_flip(ship->sprite, 0);
	ship->move = 1;

	if(ship->y < 100){
		return 1;
	} else {
		ship->y--;
		return 0;
	}
}

int ship_bottom(struct Ship* ship) {
	sprite_set_vertical_flip(ship->sprite, 0);
	ship->move = 1;

	if(ship->y > 140){
		return 1;
	} else {
		ship->y++;
		return 0;
	}
}

void ship_stop(struct Ship* ship) {
    ship->move = 0;
    ship->frame = 0;
    ship->counter = 7;
    sprite_set_offset(ship->sprite, ship->frame);
}

/* update the ship */
void ship_update(struct Ship* ship) {
    if (ship->move) {
        ship->counter++;
    }

    sprite_position(ship->sprite, ship->x, ship->y);
}

void fire(struct Laser* laser){

}

void laser_update(struct Laser* laser) {
	laser->y = (laser->y - 3);
}

/* the main function */
int main( ) {
    /* we set the mode to mode 0 with bg0 on */
    *display_control = MODE0 | BG0_ENABLE | SPRITE_ENABLE | SPRITE_MAP_1D;

    /* setup the background 0 */
    setup_background();

    /* setup the sprite image data */
    setup_sprite_image();

    /* clear all the sprites on screen now */
    sprite_clear();

    /* create the ship */
    struct Ship ship;
    ship_init(&ship);

	//create the laser
	struct Laser laser;
	laser_init(&laser);

    /* set initial scroll to 0 */
    int xscroll = 0;
	int yscroll = 8;

    /* loop forever */
    while (1) {
		//scroll the background
		yscroll--;

        /* update the ship */
        ship_update(&ship);

        /* now the arrow keys move the ship */
        if (button_pressed(BUTTON_RIGHT)) {
            if (ship_right(&ship)) {
                xscroll++;
            }
        } else if (button_pressed(BUTTON_LEFT)) {
            if (ship_left(&ship)) {
                xscroll--;
            }
        } else {
            ship_stop(&ship);
        }

		if (button_pressed(BUTTON_UP)) {
			if(ship_top(&ship)){
				yscroll--;
			}
		} else if (button_pressed(BUTTON_DOWN)) {
			ship_bottom(&ship);
		}

		if (button_pressed(BUTTON_A)) {
			laser_update(&laser);
		}
        /* wait for vblank before scrolling and moving sprites */
        wait_vblank();
        *bg0_x_scroll = xscroll;
		*bg0_y_scroll = yscroll;
        sprite_update_all();

        /* delay some */
        delay(300);
    }
}

/* the game boy advance uses "interrupts" to handle certain situations
 * for now we will ignore these */
void interrupt_ignore( ) {
    /* do nothing */
}


/* this table specifies which interrupts we handle which way
 * for now, we ignore all of them */
typedef void (*intrp)( );
const intrp IntrTable[13] = {
    interrupt_ignore,   /* V Blank interrupt */
    interrupt_ignore,   /* H Blank interrupt */
    interrupt_ignore,   /* V Counter interrupt */
    interrupt_ignore,   /* Timer 0 interrupt */
    interrupt_ignore,   /* Timer 1 interrupt */
    interrupt_ignore,   /* Timer 2 interrupt */
    interrupt_ignore,   /* Timer 3 interrupt */
    interrupt_ignore,   /* Serial communication interrupt */
    interrupt_ignore,   /* DMA 0 interrupt */
    interrupt_ignore,   /* DMA 1 interrupt */
    interrupt_ignore,   /* DMA 2 interrupt */
    interrupt_ignore,   /* DMA 3 interrupt */
    interrupt_ignore,   /* Key interrupt */
};

