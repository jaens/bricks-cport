#include "util.h"

//
// BIOS calls
//

static void bios_set_video_mode(byte mode)
{
    int ax = mode; // set high byte to 0
    // ia16 gcc does not handle %bp being a clobbered register
    asm volatile(
        "pushw %%bp\n"
        "int $0x10\n"
        "popw %%bp\n"
        : "+a"(ax)
        :
        : "cc", "si", "di");
}

static byte bios_read_modifier_keys()
{
    byte al;
    asm volatile("movb $0x02,%%ah\n"
                 "int $0x16\n"
                 : "=Ral"(al)
                 :
                 : "cc");
    return al;
}

static uint bios_get_ticks()
{
    uint dx;
    asm volatile("movb $0,%%ah\n"
                 "int $0x1a\n"
                 : "=d"(dx)
                 :
                 : "cc", "ax", "cx");
    return dx;
}

//
// Global variables
//

static int score = 0; // Current score
static int balls = 4; // Remaining balls

static int bricks; // Remaining bricks

static int ball_x, ball_y;   // Coordinate of ball (8.8 fixed point fraction)
static int ball_xs, ball_ys; // Speed of ball (8.8 fraction)

static byte beep; // Frame count to turn off sound

static int old_time;

//
// Code
//

// wait_frame.2:
static void turn_off_sound()
{
    byte al;
    // Disconnect speaker from PIT
    inb(0x61, al);
    outb(0x61, al & ~3 /*0xFC*/);
}

static void wait_frame()
{
    uint time;
    // Wait for change
    while ((time = bios_get_ticks()) == old_time)
    {
    }
    old_time = time;
    // Decrease time to turn off beep
    if (!--beep)
    {
        turn_off_sound();
    }
}

// Convert color and character into video memory word
#define ATTR(color, ch) (((byte)(color) << 8) | (byte)(ch))
// Convert coordinates to position in video segment
#define SCREEN_POS(row, col) ((row)*80 * 2 + (col)*2)

// Standard CGA palette colors
enum
{
    BLUE = 0x1,
    BROWN = 0x6,
    LIGHT_GREEN = 0xA,
};

// DOS ASCII block drawing characters
enum
{
    WALL_CHAR = 0xB1,   // Light shade
    BRICK_CHAR = 0xDB,  // Full block
    BALL_CHAR = 0xDC,   // Lower half block
    PADDLE_CHAR = 0xDF, // Upper half block
};

// (label update_score.1:)
static void update_score_inner(uint *pos /*ptr to %BX*/, int num /*%AX*/)
{
    do
    {
        int quotient = 0; // %CX
        // Division by subtraction
        do
        {
            quotient += 1;
            num -= 10;
        } while (num > 0);
        // Convert remainder to ASCII digit + color
        // (num is negative so it goes "backwards" in ASCII)
        num += ATTR(LIGHT_GREEN, '9' + 1) /* 0x0A3A */;

        // Output (backwards)
        vidw(*pos) = num;
        *pos -= 2;

        int num_tmp = num;
        num = quotient;
        quotient = num_tmp;
        // Quotient is zero?
    } while (--num);

    // Output an empty space (num == 0)
    vidw(*pos) = num;
    *pos -= 2;
}

static void update_score()
{
    // Point to bottom right corner
    uint pos = SCREEN_POS(24, 76) /*0x0F98*/;
    update_score_inner(&pos, score);
    update_score_inner(&pos, balls);
}

static uint locate_ball(uint x /*%BX*/, uint y /*%AX*/) /*return %BX*/
{
    return HI(y) * 80 * 2 + HI(x) * 2;
}

// Convert HZ to PIT timer count
#define PIT_HZ(freq) (1193180 / (freq))

static void speaker(uint period /*%CX*/)
{
    outb(0x43, 0xB6);       // Setup PIT, timer 2
    outb(0x42, LO(period)); // Low byte of timer count
    outb(0x42, HI(period)); // High byte of timer count

    byte al;
    inb(0x61, al);
    outb(0x61, al | 3); // Connect PC speaker to timer 2

    beep = 3; // Duration
}

int main()
{
    // Text mode 80x25x16 colors
    bios_set_video_mode(2);

another_level:
    bricks = 273;

    // Draw playfield
    {
        uint pos = 0;
        uint_lh attr = {ATTR(BLUE, WALL_CHAR) /* 0x01B1 */};
        // Top border
        for (int i = 80; i; i--)
            vid_stosw(pos, attr.word);

        for (int row = 24; row; row--)
        {
            vid_stosw(pos, attr.word); // Left border

            attr.word = ' '; // Empty space

            int row_tmp = row;
            if (row_tmp < 23 && (row_tmp -= 15) > 0)
            {
                attr.lo = BRICK_CHAR /* 0xDB */;
                attr.hi = row_tmp;
            }

            for (int brick = 39; brick; brick--)
            {
                vid_stosw(pos, attr.word);
                vid_stosw(pos, attr.word);

                // Increase attribute color
                attr.hi += 1;
                if (attr.hi == 0x08)
                {
                    attr.hi = BLUE;
                }
            }

            // Right border
            attr.word = ATTR(BLUE, WALL_CHAR) /* 0x01B1 */;
            vid_stosw(pos, attr.word);
        }
    }

    // Position of paddle
    uint paddle_pos = SCREEN_POS(24, 37) /* 0x0F4A */; // %DI

another_ball:
    ball_x = 0x2800;       // Center X
    ball_y = 0x1400;       // Center Y
    ball_xs = ball_ys = 0; // Static on screen (not moving)

    beep = 1;

    // Don't erase ball yet (point to after end of video data)
    uint ball_pos = 0x0FFE; // %SI

    // label game_loop:
    for (;;)
    {
        // Wait 1/18.2 secs.
        wait_frame();

        // Erase ball
        vidw(ball_pos) = 0;

        update_score();

        const byte mods = bios_read_modifier_keys();
        if (mods & 0x04 /* Left ctrl */)
        {
            // Erase right side of paddle
            vidc[paddle_pos + 6] = 0;
            vidc[paddle_pos + 8] = 0;
            // Move paddle to left
            paddle_pos -= 4;

            const uint LEFT_LIMIT = SCREEN_POS(24, 1) /* 0x0F02 */;
            if (paddle_pos < LEFT_LIMIT)
                paddle_pos = LEFT_LIMIT;
        }
        if (mods & 0x08 /* Left alt */)
        {
            // Erase left side of paddle
            vid_stosw(paddle_pos, 0);
            vid_stosw(paddle_pos, 0);

            const uint RIGHT_LIMIT = SCREEN_POS(24, 74) /* 0x0F94 */;
            if (paddle_pos > RIGHT_LIMIT)
                paddle_pos = RIGHT_LIMIT;
        }
        if (mods & 0x02 /* Left shift */)
        {
            // Ball moving?
            if (ball_xs + ball_ys == 0)
            {
                // Setup movement of ball
                ball_xs = 0xFF40; // -1.25
                ball_ys = 0xFF80; // -1.5
            }
        }

        // Draw paddle
        {
            const uint paddle_attr = ATTR(LIGHT_GREEN, PADDLE_CHAR) /* 0x0ADF */;
            uint paddle_tmp = paddle_pos;
            vid_stosw(paddle_tmp, paddle_attr);
            vid_stosw(paddle_tmp, paddle_attr);
            vid_stosw(paddle_tmp, paddle_attr);
            vid_stosw(paddle_tmp, paddle_attr);
            vid_stosw(paddle_tmp, paddle_attr);
        }

        // Draw ball
        ball_pos = locate_ball(ball_x, ball_y);
        {
            byte color = BROWN << 4; // As background color
            // Y-coordinate half fraction?
            if (ball_y & 0x80)
            {
                color = BROWN; // Interchange colors for smooth movement
            }
            uint ball_attr = ATTR(color, BALL_CHAR /*0xDC*/);
            // Draw
            vidw(ball_pos) = ball_attr;
        }

        // Ball position
        uint bx; // %BX
        uint by; // %AX
        // Position update / bounce loop (label .14:)
        for (;;)
        {
            // Add movement speed
            bx = ball_x + ball_xs;
            by = ball_y + ball_ys;

            const uint ball_pos_next = locate_ball(bx, by);
            const byte c = vidc[ball_pos_next]; // %AL
            // Touching borders
            if (c == WALL_CHAR /* 0xB1 */)
            {
                speaker(PIT_HZ(220));
                if (HI(bx) == 79 /* 0x4F */ || HI(bx) == 0)
                {
                    // Negate X-speed if touches sides
                    ball_xs = -ball_xs;
                }
                if (HI(by) == 0)
                {
                    ball_ys = -ball_ys;
                }
            }
            // Touching paddle
            else if (c == PADDLE_CHAR /* 0xDF */)
            {
                // New X speed for ball
                int new_xs = (ball_pos_next - paddle_pos - 4) << 6;
                ball_xs = new_xs;
                ball_ys = 0xFF80; // Update Y speed for ball
                speaker(PIT_HZ(440));
            }
            // Touching brick
            else if (c == BRICK_CHAR /* 0xDB */)
            {
                speaker(PIT_HZ(880));
                int brick_pos = ball_pos_next; // %BX
                // Aligned with brick start?
                if (!(brick_pos & 2))
                {
                    brick_pos -= 2; // Align
                }
                // Erase brick
                vidw(brick_pos) = 0;
                vidw(brick_pos + 2) = 0;

                score++;
                // Negate Y speed (rebound)
                ball_ys = -ball_ys;
                // Fully completed?
                if (!--bricks)
                {
                    // Start another level
                    goto another_level;
                }
            }
            else
            {
                break;
            }
        }

        // Update ball position
        ball_x = bx;
        ball_y = by;
        // Ball exited through bottom?
        if (HI(by) == 25 /*0x19*/)
        {
            speaker(PIT_HZ(110));
            vidw(ball_pos) = 0; // Erase ball
            if (!--balls)
            {
                turn_off_sound();
                return 0; // Exit to DOS
            }
            goto another_ball;
        }
    } // goto game_loop;
}

// Overwrite some useless code in the ia16-gcc standard C library
void __call_exitprocs() {}
