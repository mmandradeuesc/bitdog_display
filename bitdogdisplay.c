#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "bitdogdisplay.pio.h"
#include "ssd1306.h"
#include "font.h"

#define NUM_PIXELS 25
#define OUT_PIN 7
#define UART_ID uart1
#define BAUD_RATE 115200
#define UART_TX_PIN 2
#define UART_RX_PIN 3
#define BUTTON_A 5
#define BUTTON_B 6
#define LED_RED 13
#define LED_GREEN 11
#define LED_BLUE 12
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C

// Protótipos de funções
void update_display(const char* text, ssd1306_t* ssd);
void send_uart_message(const char* message);
void display_number(int num, PIO pio, uint sm, uint32_t color);
void gpio_callback(uint gpio, uint32_t events);
int64_t debounce_callback(alarm_id_t id, void *user_data);

// Variáveis globais
volatile int current_number = 0;
volatile bool button_a_pressed = false;
volatile bool button_b_pressed = false;
volatile bool green_led_state = false;
volatile bool blue_led_state = false;
bool led_state = false;
uint32_t matrix_color = 0x00FFFFFF;

// Padrões numéricos corrigidos
const uint8_t number_patterns[10][25] = {
    {
        1, 1, 1, 1, 1,
        1, 0, 0, 0, 1,
        1, 0, 0, 0, 1,
        1, 0, 0, 0, 1,
        1, 1, 1, 1, 1  
    },// Número 0
    {
        0, 1, 1, 1, 0,  
        0, 0, 1, 0, 0,  
        0, 0, 1, 0, 0,  
        0, 1, 1, 0, 0,  
        0, 0, 1, 0, 0
    },   // Número 

    {
        1, 1, 1, 1, 1, 
        1, 0, 0, 0, 0,  
        1, 1, 1, 1, 1,  
        0, 0, 0, 0, 1,  
        1, 1, 1, 1, 1   
    }, // Número 2 

    {
        1, 1, 1, 1, 1,
        0, 0, 0, 0, 1,
        1, 1, 1, 1, 1,
        0, 0, 0, 0, 1,
        1, 1, 1, 1, 1  
    }, // Número 3

    {
        1, 0, 0, 0, 0,  
        0, 0, 0, 0, 1,  
        1, 1, 1, 1, 1,  
        1, 0, 0, 0, 1,  
        1, 0, 0, 0, 1   
    },  // Número 4

    {
        1, 1, 1, 1, 1,  
        0, 0, 0, 0, 1,  
        1, 1, 1, 1, 1,  
        1, 0, 0, 0, 0,  
        1, 1, 1, 1, 1   
    },  // Número 5

    {
        1, 1, 1, 1, 1,  
        1, 0, 0, 0, 1,  
        1, 1, 1, 1, 1,  
        1, 0, 0, 0, 0,  
        1, 1, 1, 1, 1   
    },  // Número 6

    {
        0, 0, 0, 0, 1, 
        0, 1, 0, 0, 0,  
        0, 0, 1, 0, 0,  
        0, 0, 0, 1, 0,  
        1, 1, 1, 1, 1
    },  // Número 7

    {
        1, 1, 1, 1, 1,  
        1, 0, 0, 0, 1, 
        1, 1, 1, 1, 1, 
        1, 0, 0, 0, 1,  
        1, 1, 1, 1, 1   
    },  // Número 8

    {
        1, 1, 1, 1, 1, 
        0, 0, 0, 0, 1, 
        1, 1, 1, 1, 1,  
        1, 0, 0, 0, 1,  
        1, 1, 1, 1, 1   
    }   // Número 9
};

// Função para enviar mensagem pela UART
void send_uart_message(const char* message) {
    uart_puts(UART_ID, message);
    uart_puts(UART_ID, "\r\n");
    sleep_ms(10); 
}

// Função para atualizar o display OLED
void update_display(const char* text, ssd1306_t* ssd) {
    ssd1306_fill(ssd, false);
    ssd1306_draw_string(ssd, text, 0, 0);
    ssd1306_send_data(ssd);
}

// Função de callback para debounce
int64_t debounce_callback(alarm_id_t id, void *user_data) {
    uint gpio_num = (uint)(uintptr_t)user_data;
    if (!gpio_get(gpio_num)) {
        if (gpio_num == BUTTON_A) button_a_pressed = true;
        if (gpio_num == BUTTON_B) button_b_pressed = true;
    }
    return 0;
}

// Função de callback para botões
void gpio_callback(uint gpio, uint32_t events) {
    if (gpio == BUTTON_A || gpio == BUTTON_B) {
        static alarm_id_t debounce_alarm;
        cancel_alarm(debounce_alarm);
        debounce_alarm = add_alarm_in_ms(50, debounce_callback, (void*)(uintptr_t)gpio, true);
    }
}

void display_number(int num, PIO pio, uint sm, uint32_t color) {
    num = (num % 10 + 10) % 10;
    
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 5; col++) {
            int index = row * 5 + col;
            uint32_t pixel_color = number_patterns[num][index] ? color : 0x000000;
            pio_sm_put_blocking(pio, sm, pixel_color << 8u);
        }
    }
}

int main() {
    // Inicialização do stdio
    stdio_init_all();

    // Configuração da UART
    uart_init(UART_ID, BAUD_RATE);
    uint actual_baud = uart_set_baudrate(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    gpio_pull_up(UART_TX_PIN);
    gpio_pull_up(UART_RX_PIN);

    if (uart_is_enabled(UART_ID)) {
    gpio_put(LED_GREEN, 1); // Acende LED verde se UART ok
        } else {
    gpio_put(LED_RED, 1);   // Acende LED vermelho se falhou
        }

    // Inicialização I2C
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Inicialização do display OLED
    ssd1306_t ssd;
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);

    // Configuração dos LEDs
    gpio_init(LED_RED);
    gpio_init(LED_GREEN);
    gpio_init(LED_BLUE);
    gpio_set_dir(LED_RED, GPIO_OUT);
    gpio_set_dir(LED_GREEN, GPIO_OUT);
    gpio_set_dir(LED_BLUE, GPIO_OUT);
    
    // Inicialmente todos os LEDs RGB desligados
    gpio_put(LED_RED, 0);
    gpio_put(LED_GREEN, 0);
    gpio_put(LED_BLUE, 0);

    // Configuração dos botões
    gpio_init(BUTTON_A);
    gpio_init(BUTTON_B);
    gpio_set_dir(BUTTON_A, GPIO_IN);
    gpio_set_dir(BUTTON_B, GPIO_IN);
    gpio_pull_up(BUTTON_A);
    gpio_pull_up(BUTTON_B);
    
    gpio_set_irq_enabled(BUTTON_A, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BUTTON_B, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_callback(gpio_callback);
    irq_set_enabled(IO_IRQ_BANK0, true);

    // Configuração da matriz de LEDs
    PIO pio = pio0;
    uint offset = pio_add_program(pio, &bitdogdisplay_program);
    uint sm = pio_claim_unused_sm(pio, true);
    bitdogdisplay_program_init(pio, sm, offset, OUT_PIN);
    
    // Mensagem inicial
    update_display("Sistema Iniciado", &ssd);
    send_uart_message("Sistema iniciado e pronto para receber comandos");

    while (true) {
        // Verifica entrada UART
        if (uart_is_readable(UART_ID)) {
            char c = uart_getc(UART_ID);
            if (c >= '0' && c <= '9') {  // Ignora caracteres não numéricos
                char display_text[2] = {c, '\0'};
                
                // Mostra o caractere no display
                update_display(display_text, &ssd);
                
                // Exibe o número na matriz de LEDs
                int num = c - '0';
                display_number(num, pio, sm, matrix_color);
                send_uart_message("Numero recebido e exibido na matriz");
            }
        }

        // Tratamento do Botão A (LED Verde)
        if (button_a_pressed) {
            button_a_pressed = false;
            green_led_state = !green_led_state;
            gpio_put(LED_GREEN, green_led_state);
            
            // Atualiza display e envia mensagem UART
            update_display(green_led_state ? "LED Verde ON" : "LED Verde OFF", &ssd);
            send_uart_message(green_led_state ? "LED Verde foi ligado" : "LED Verde foi desligado");
        }

        // Tratamento do Botão B (LED Azul)
        if (button_b_pressed) {
            button_b_pressed = false;
            blue_led_state = !blue_led_state;
            gpio_put(LED_BLUE, blue_led_state);
            
            // Atualiza display e envia mensagem UART
            update_display(blue_led_state ? "LED Azul ON" : "LED Azul OFF", &ssd);
            send_uart_message(blue_led_state ? "LED Azul foi ligado" : "LED Azul foi desligado");
        }

        tight_loop_contents();
    }
}