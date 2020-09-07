#include <LovyanGFX.hpp>
#include "makerfabs_pin.h"
#include "touch.h"
#include <SPI.h>

// instead of using TFT.width() and TFT.height() set constant values
// (we can change the size of the game easily that way)
#define TFTW 320 // screen width
#define TFTH 480 // screen height
#define TFTW2 160 // half screen width
#define TFTH2 240 // half screen height
// game constant
#define SPEED 1
#define GRAVITY 9.8
#define JUMP_FORCE 2.15
#define SKIP_TICKS 20.0 // 1000 / 50fps
#define MAX_FRAMESKIP 5
// bird size
#define BIRDW 8  // bird width
#define BIRDH 8  // bird height
#define BIRDW2 4 // half width
#define BIRDH2 4 // half height
// game_pipe size
#define PIPEW 36     // game_pipe width
#define GAPHEIGHT 100 // game_pipe gap height
// floor size
#define FLOORH 40 // floor height (from bottom of the screen)
// grass size
#define GRASSH 10 // grass height (inside floor, starts at floor y)

struct LGFX_Config
{
    static constexpr spi_host_device_t spi_host = ESP32_TSC_9488_LCD_SPI_HOST;
    static constexpr int dma_channel = 1;
    static constexpr int spi_sclk = ESP32_TSC_9488_LCD_SCK;
    static constexpr int spi_mosi = ESP32_TSC_9488_LCD_MOSI;
    static constexpr int spi_miso = ESP32_TSC_9488_LCD_MISO;
};

static lgfx::LGFX_SPI<LGFX_Config> TFT;
static LGFX_Sprite sprite(&TFT);
static lgfx::Panel_ILI9488 panel;

// background
const unsigned int BCKGRDCOL = TFT.color565(138, 235, 244);
// bird
const unsigned int BIRDCOL = TFT.color565(255, 254, 174);
// game_pipe
const unsigned int PIPECOL = TFT.color565(99, 255, 78);
// game_pipe highlight
const unsigned int PIPEHIGHCOL = TFT.color565(250, 255, 250);
// game_pipe seam
const unsigned int PIPESEAMCOL = TFT.color565(0, 0, 0);
// floor
const unsigned int FLOORCOL = TFT.color565(246, 240, 163);
// grass (col2 is the stripe color)
const unsigned int GRASSCOL = TFT.color565(141, 225, 87);
const unsigned int GRASSCOL2 = TFT.color565(156, 239, 88);

// bird sprite
// bird sprite colors (Cx name for values to keep the array readable)
#define C0 BCKGRDCOL
#define C1 TFT.color565(195, 165, 75)
#define C2 BIRDCOL
#define C3 TFT_WHITE
#define C4 TFT_RED
#define C5 TFT.color565(251, 216, 114)
static unsigned int birdcol[] =
    {C0, C0, C1, C1, C1, C1, C1, C0,
     C0, C1, C2, C2, C2, C1, C3, C1,
     C0, C2, C2, C2, C2, C1, C3, C1,
     C1, C1, C1, C2, C2, C3, C1, C1,
     C1, C2, C2, C2, C2, C2, C4, C4,
     C1, C2, C2, C2, C1, C5, C4, C0,
     C0, C1, C2, C1, C5, C5, C5, C0,
     C0, C0, C1, C5, C5, C5, C0, C0};

// bird structure
static struct BIRD
{
    unsigned int x, y, old_y;
    unsigned int col;
    float vel_y;
} bird;
// game_pipe structure
static struct GAME_PIPE
{
    int x, gap_y;
    unsigned int col;
} game_pipe;

// score
static short score;
// temporary x and y var
static short tmpx, tmpy;

// ---------------
// initial setup
// ---------------
void setup()
{
    Serial.begin(115200);

    Wire.begin(ESP32_TSC_9488_I2C_SDA, ESP32_TSC_9488_I2C_SCL);
    byte error, address;

    Wire.beginTransmission(TOUCH_I2C_ADD);
    error = Wire.endTransmission();

    if (error == 0)
    {
        Serial.print("I2C device found at address 0x");
        Serial.print(TOUCH_I2C_ADD, HEX);
        Serial.println("  !");
    }
    else if (error == 4)
    {
        Serial.print("Unknown error at address 0x");
        Serial.println(TOUCH_I2C_ADD, HEX);
    }
    push_button();

    set_tft();
    TFT.begin();
}

// ---------------
// main loop
// ---------------
void loop()
{
    game_start();
    game_loop();
    game_over();
}

// ---------------
// game loop
// ---------------
void game_loop()
{
    // ===============
    // prepare game variables
    // draw floor
    // ===============
    // instead of calculating the distance of the floor from the screen height each time store it in a variable
    unsigned int GAMEH = TFTH - FLOORH;
    Serial.println(GAMEH);

    // draw the floor once, we will not overwrite on this area in-game
    // black line
    TFT.drawFastHLine(0, GAMEH, TFTW, TFT_BLACK);
    // grass and stripe
    TFT.fillRect(0, GAMEH + 1, TFTW2, GRASSH, GRASSCOL);
    TFT.fillRect(TFTW2, GAMEH + 1, TFTW2, GRASSH, GRASSCOL2);
    // black line
    TFT.drawFastHLine(0, GAMEH + GRASSH, TFTW, TFT_BLACK);
    // mud
    TFT.fillRect(0, GAMEH + GRASSH + 1, TFTW, FLOORH - GRASSH, FLOORCOL);
    // grass x position (for stripe animation)
    int grassx = TFTW;
    // game loop time variables
    double delta, old_time, next_game_tick, current_time;
    next_game_tick = current_time = millis();
    int loops;
    // passed game_pipe flag to count score
    bool passed_pipe = false;
    // temp var for setAddrWindow
    unsigned int px;

    while (1)
    {
        loops = 0;
        while (millis() > next_game_tick && loops < MAX_FRAMESKIP)
        {
            // ===============
            // input
            // ===============
            if (push_button())
            {
                // if the bird is not too close to the top of the screen apply jump force
                if (bird.y > BIRDH2 * 0.5)
                    bird.vel_y = -JUMP_FORCE;
                // else zero velocity
                else
                    bird.vel_y = 0;
            }

            // ===============
            // update
            // ===============
            // calculate delta time
            // ---------------
            old_time = current_time;
            current_time = millis();
            delta = (current_time - old_time) / 1000;

            // bird
            // ---------------
            bird.vel_y += GRAVITY * delta;
            bird.y += bird.vel_y;

            // game_pipe
            // ---------------
            game_pipe.x -= SPEED;
            // if game_pipe reached edge of the screen reset its position and gap
            if (game_pipe.x < -PIPEW)
            {
                game_pipe.x = TFTW;
                game_pipe.gap_y = random(10, GAMEH - (10 + GAPHEIGHT));
            }

            // ---------------
            next_game_tick += SKIP_TICKS;
            loops++;
        }

        // ===============
        // draw
        // ===============
        // game_pipe
        // ---------------
        // we save cycles if we avoid drawing the game_pipe when outside the screen
        if (game_pipe.x >= 0 && game_pipe.x < TFTW)
        {
            // game_pipe color
            TFT.drawFastVLine(game_pipe.x + 3, 0, game_pipe.gap_y, PIPECOL);
            TFT.drawFastVLine(game_pipe.x + 3, game_pipe.gap_y + GAPHEIGHT + 1, GAMEH - (game_pipe.gap_y + GAPHEIGHT + 1), PIPECOL);
            // highlight
            TFT.drawFastVLine(game_pipe.x, 0, game_pipe.gap_y, PIPEHIGHCOL);
            TFT.drawFastVLine(game_pipe.x, game_pipe.gap_y + GAPHEIGHT + 1, GAMEH - (game_pipe.gap_y + GAPHEIGHT + 1), PIPEHIGHCOL);
            // bottom and top border of game_pipe
            TFT.drawPixel(game_pipe.x, game_pipe.gap_y, PIPESEAMCOL);
            TFT.drawPixel(game_pipe.x, game_pipe.gap_y + GAPHEIGHT, PIPESEAMCOL);
            // game_pipe seam
            TFT.drawPixel(game_pipe.x, game_pipe.gap_y - 6, PIPESEAMCOL);
            TFT.drawPixel(game_pipe.x, game_pipe.gap_y + GAPHEIGHT + 6, PIPESEAMCOL);
            TFT.drawPixel(game_pipe.x + 3, game_pipe.gap_y - 6, PIPESEAMCOL);
            TFT.drawPixel(game_pipe.x + 3, game_pipe.gap_y + GAPHEIGHT + 6, PIPESEAMCOL);
        }
        // erase behind game_pipe
        if (game_pipe.x <= TFTW)
            TFT.drawFastVLine(game_pipe.x + PIPEW, 0, GAMEH, BCKGRDCOL);

        // bird
        // ---------------
        tmpx = BIRDW - 1;
        do
        {
            px = bird.x + tmpx + BIRDW;
            // clear bird at previous position stored in old_y
            // we can't just erase the pixels before and after current position
            // because of the non-linear bird movement (it would leave 'dirty' pixels)
            tmpy = BIRDH - 1;
            do
            {
                TFT.drawPixel(px, bird.old_y + tmpy, BCKGRDCOL);
            } while (tmpy--);
            // draw bird sprite at new position
            tmpy = BIRDH - 1;
            do
            {
                TFT.drawPixel(px, bird.y + tmpy, birdcol[tmpx + (tmpy * BIRDW)]);
            } while (tmpy--);
        } while (tmpx--);
        // save position to erase bird on next draw
        bird.old_y = bird.y;

        // grass stripes
        // ---------------
        grassx -= SPEED;
        if (grassx < 0)
            grassx = TFTW;
        TFT.drawFastVLine(grassx % TFTW, GAMEH + 1, GRASSH - 1, GRASSCOL);
        TFT.drawFastVLine((grassx + 64) % TFTW, GAMEH + 1, GRASSH - 1, GRASSCOL2);


        // ===============
        // collision
        // ===============
        // if the bird hit the ground game over
        if (bird.y > GAMEH - BIRDH)
            break;
        // checking for bird collision with game_pipe
        if (bird.x + BIRDW >= game_pipe.x - BIRDW2 && bird.x <= game_pipe.x + PIPEW - BIRDW)
        {
            // bird entered a game_pipe, check for collision
            if (bird.y < game_pipe.gap_y || bird.y + BIRDH > game_pipe.gap_y + GAPHEIGHT)
                break;
            else
                passed_pipe = true;
        }
        // if bird has passed the game_pipe increase score
        else if (bird.x > game_pipe.x + PIPEW - BIRDW && passed_pipe)
        {
            passed_pipe = false;
            // erase score with background color
            TFT.setTextColor(BCKGRDCOL);
            TFT.setCursor(TFTW2, 4);
            TFT.setTextSize(3);
            TFT.print(score);
            // set text color back to white for new score
            TFT.setTextColor(TFT_WHITE);
            // increase score since we successfully passed a game_pipe
            score++;
        }


        // update score
        // ---------------
        TFT.setTextSize(3);
        TFT.setCursor(TFTW2, 4);
        TFT.print(score);
    }

    // add a small delay to show how the player lost
    delay(1000);
}

// ---------------
// game start
// ---------------
void game_start()
{
    TFT.fillScreen(TFT_BLACK);
    TFT.fillRect(10, TFTH2 - 20, TFTW - 20, 1, TFT_WHITE);
    TFT.fillRect(10, TFTH2 + 32, TFTW - 20, 1, TFT_WHITE);
    TFT.setTextColor(TFT_WHITE);
    TFT.setTextSize(3);
    // half width - num int * int width in pixels
    TFT.setCursor(TFTW2 - (6 * 9), TFTH2 - 16);
    TFT.println("FLAPPY");
    TFT.setTextSize(3);
    TFT.setCursor(TFTW2 - (6 * 9), TFTH2 + 8);
    TFT.println("-BIRD-");
    TFT.setTextSize(0);
    TFT.setCursor(10, TFTH2 - 28);
    TFT.println("ESP32");
    TFT.setCursor(TFTW2 - (12 * 3) - 1, TFTH2 + 34);
    TFT.println("press button");
    while (1)
    {
        // wait for push button
        if (push_button())
            break;
    }

    // init game settings
    game_init();
}

// ---------------
// game init
// ---------------
void game_init()
{
    // clear screen
    TFT.fillScreen(BCKGRDCOL);
    // reset score
    score = 0;
    // init bird
    bird.x = 20;
    bird.y = bird.old_y = TFTH2 - BIRDH;
    bird.vel_y = -JUMP_FORCE;
    tmpx = tmpy = 0;
    // generate new random seed for the game_pipe gape
    randomSeed(analogRead(0));
    // init game_pipe
    game_pipe.x = TFTW;
    game_pipe.gap_y = random(20, TFTH - 60);
}

// ---------------
// game over
// ---------------
void game_over()
{
    TFT.fillScreen(TFT_BLACK);
    TFT.setTextColor(TFT_WHITE);
    TFT.setTextSize(2);
    // half width - num int * int width in pixels
    TFT.setCursor(TFTW2 - (9 * 6), TFTH2 - 4);
    TFT.println("GAME OVER");
    TFT.setTextSize(0);
    TFT.setCursor(10, TFTH2 - 14);
    TFT.print("score: ");
    TFT.print(score);
    TFT.setCursor(TFTW2 - (12 * 3), TFTH2 + 12);
    TFT.println("press button");
    while (1)
    {
        // wait for push button
        if (push_button())
            break;
    }
}

//
int push_button()
{
    return get_button();
}

//TFT set

void set_tft()
{
    // パネルクラスに各種設定値を代入していきます。
    // （LCD一体型製品のパネルクラスを選択した場合は、
    //   製品に合った初期値が設定されているので設定は不要です）

    // 通常動作時のSPIクロックを設定します。
    // ESP32のSPIは80MHzを整数で割った値のみ使用可能です。
    // 設定した値に一番近い設定可能な値が使用されます。
    panel.freq_write = 60000000;
    //panel.freq_write = 20000000;

    // 単色の塗り潰し処理時のSPIクロックを設定します。
    // 基本的にはfreq_writeと同じ値を設定しますが、
    // より高い値を設定しても動作する場合があります。
    panel.freq_fill = 60000000;
    //panel.freq_fill  = 27000000;

    // LCDから画素データを読取る際のSPIクロックを設定します。
    panel.freq_read = 16000000;

    // SPI通信モードを0~3から設定します。
    panel.spi_mode = 0;

    // データ読み取り時のSPI通信モードを0~3から設定します。
    panel.spi_mode_read = 0;

    // 画素読出し時のダミービット数を設定します。
    // 画素読出しでビットずれが起きる場合に調整してください。
    panel.len_dummy_read_pixel = 8;

    // データの読取りが可能なパネルの場合はtrueを、不可の場合はfalseを設定します。
    // 省略時はtrueになります。
    panel.spi_read = true;

    // データの読取りMOSIピンで行うパネルの場合はtrueを設定します。
    // 省略時はfalseになります。
    panel.spi_3wire = false;

    // LCDのCSを接続したピン番号を設定します。
    // 使わない場合は省略するか-1を設定します。
    panel.spi_cs = ESP32_TSC_9488_LCD_CS;

    // LCDのDCを接続したピン番号を設定します。
    panel.spi_dc = ESP32_TSC_9488_LCD_DC;

    // LCDのRSTを接続したピン番号を設定します。
    // 使わない場合は省略するか-1を設定します。
    panel.gpio_rst = ESP32_TSC_9488_LCD_RST;

    // LCDのバックライトを接続したピン番号を設定します。
    // 使わない場合は省略するか-1を設定します。
    panel.gpio_bl = ESP32_TSC_9488_LCD_BL;

    // バックライト使用時、輝度制御に使用するPWMチャンネル番号を設定します。
    // PWM輝度制御を使わない場合は省略するか-1を設定します。
    panel.pwm_ch_bl = -1;

    // バックライト点灯時の出力レベルがローかハイかを設定します。
    // 省略時は true。true=HIGHで点灯 / false=LOWで点灯になります。
    panel.backlight_level = true;

    // invertDisplayの初期値を設定します。trueを設定すると反転します。
    // 省略時は false。画面の色が反転している場合は設定を変更してください。
    panel.invert = false;

    // パネルの色順がを設定します。  RGB=true / BGR=false
    // 省略時はfalse。赤と青が入れ替わっている場合は設定を変更してください。
    panel.rgb_order = false;

    // パネルのメモリが持っているピクセル数（幅と高さ）を設定します。
    // 設定が合っていない場合、setRotationを使用した際の座標がずれます。
    // （例：ST7735は 132x162 / 128x160 / 132x132 の３通りが存在します）
    panel.memory_width = ESP32_TSC_9488_LCD_WIDTH;
    panel.memory_height = ESP32_TSC_9488_LCD_HEIGHT;

    // パネルの実際のピクセル数（幅と高さ）を設定します。
    // 省略時はパネルクラスのデフォルト値が使用されます。
    panel.panel_width = ESP32_TSC_9488_LCD_WIDTH;
    panel.panel_height = ESP32_TSC_9488_LCD_HEIGHT;

    // パネルのオフセット量を設定します。
    // 省略時はパネルクラスのデフォルト値が使用されます。
    panel.offset_x = 0;
    panel.offset_y = 0;

    // setRotationの初期化直後の値を設定します。
    panel.rotation = 0;

    // setRotationを使用した時の向きを変更したい場合、offset_rotationを設定します。
    // setRotation(0)での向きを 1の時の向きにしたい場合、 1を設定します。
    panel.offset_rotation = 0;

    // 設定を終えたら、LGFXのsetPanel関数でパネルのポインタを渡します。
    TFT.setPanel(&panel);
}