#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>

#include <SDL2/SDL_scancode.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"
#include "task.h"

#include "TUM_Ball.h"
#include "TUM_Draw.h"
#include "TUM_Event.h"
#include "TUM_Sound.h"
#include "TUM_Utils.h"
#include "TUM_Font.h"

#include "AsyncIO.h"

#define mainGENERIC_PRIORITY (tskIDLE_PRIORITY)
#define mainMAX_PRIORITY (configMAX_PRIORITIES-1)
#define mainGENERIC_STACK_SIZE ((unsigned short)2560)
#define NUM_TIMERS 5

 /* An array to hold handles to the created timers. */
 TimerHandle_t xTimers[ NUM_TIMERS ];

static TaskHandle_t DemoTask = NULL;
static TaskHandle_t FakeKeyISRTask = NULL;
static TaskHandle_t myTask = NULL;
static TaskHandle_t myTask2 = NULL;
static TaskHandle_t pingTask = NULL;
static TaskHandle_t pongTask = NULL;
SemaphoreHandle_t ppSem;

typedef struct buttons_buffer {
    unsigned char buttons[SDL_NUM_SCANCODES];
    SemaphoreHandle_t lock;
} buttons_buffer_t;

static buttons_buffer_t buttons = { 0 };

void xGetButtonInput(void)
{
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
    	//anyone who attempts to take the SEM now will BLOCK
        xQueueReceive(buttonInputQueue, &buttons.buttons, 0);
    	//anyone who attempts to take the SEM now will BLOCK
        xSemaphoreGive(buttons.lock);
    	//anyone who attempts or who attempted to take the SEM now will continue
    }
}

#define KEYCODE(CHAR) SDL_SCANCODE_##CHAR

void fakeKeyISR(char keycode)
{
    printf("fake ISR fired: %c \r\n", keycode);
}

void vMyTask(void *pvParameters)
{
	int mycounter = 0;
	char* taskname = pcTaskGetName(xTaskGetCurrentTaskHandle());
	while (1) 
    {
        printf("%s task: %d\r\n", taskname, mycounter++);
        // Basic sleep of 1000 milliseconds
        vTaskDelay((TickType_t)500);
    }
}

void count_things(int new_errs, int* cerrs)
{
	int temp_errs = *cerrs;
	*cerrs = temp_errs + new_errs;
	printf("new thing count is %d\n", *cerrs);
}


long long int pingpong = 0;
void vPingPongTask(void *pvParameters)
{
	int cerrs = 0;
	char* taskname = pcTaskGetName(xTaskGetCurrentTaskHandle());
	while (1) 
    {
	    if (xSemaphoreTake(ppSem, 0) == pdTRUE) {
	    	printf("%s task: with task_param: %s, %lld\r\n", taskname, (char*)pvParameters, pingpong);
	        // Basic sleep of 1000 milliseconds
	        pingpong++;
	        if(pingpong % 2)
	        {
	        	count_things(1, &cerrs);
	        }
        	vTaskDelay((TickType_t)1000);
	        xSemaphoreGive(ppSem);
	    }
        vTaskDelay((TickType_t)100);
    }
}

void vFakeKeyISRFireTask(void *pvParameters)
{
	static int looper;
	while (1) 
    {
        printf("waiting for key\r\n");
        xQueueReceive(buttonInputQueue, &buttons.buttons, portMAX_DELAY);
        if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) 
        {
            for (looper = 4; looper <= 29; looper++) 
            {
                if (buttons.buttons[looper]) 
                {
                    printf("Key pressed %c\r\n", looper - 4 + 'a');
                    fakeKeyISR(looper - 4 + 'a');
                }
            }
            for (looper = 30; looper <= 39; looper++) 
            {
                if (buttons.buttons[looper]) 
                {
                    printf("Key pressed %d\r\n", (looper - 29) % 10);
                    fakeKeyISR((looper - 29) % 10 + '0');
                }
            }
            for(looper=0; looper<SDL_NUM_SCANCODES; looper++)
            {
                if(buttons.buttons[looper])
                    printf("buttons[%d] = %d\r\n", looper, buttons.buttons[looper]);
            }
            xSemaphoreGive(buttons.lock);
        }
    }
}

void vDemoTask(void *pvParameters)
{
    // structure to store time retrieved from Linux kernel
    static struct timespec the_time;
    static char our_time_string[100];
    static int our_time_strings_width = 0;

    // Needed such that Gfx library knows which thread controlls drawing
    // Only one thread can call tumDrawUpdateScreen while and thread can call
    // the drawing functions to draw objects. This is a limitation of the SDL
    // backend.
    tumDrawBindThread();

    while (1) {
        tumEventFetchEvents(FETCH_EVENT_NONBLOCK); // Query events backend for new events, ie. button presses
        // xGetButtonInput(); // Update global input

        // `buttons` is a global shared variable and as such needs to be
        // guarded with a mutex, mutex must be obtained before accessing the
        // resource and given back when you're finished. If the mutex is not
        // given back then no other task can access the reseource.
        if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
            if (buttons.buttons[KEYCODE(
                                    Q)]) { // Equiv to SDL_SCANCODE_Q
                exit(EXIT_SUCCESS);
            }
            xSemaphoreGive(buttons.lock);
        }

        tumDrawClear(White); // Clear screen

        clock_gettime(CLOCK_REALTIME,
                      &the_time); // Get kernel real time

        // Format our string into our char array
        sprintf(our_time_string,
                "There has been %ld seconds since the Epoch. Press Q to quit",
                (long int)the_time.tv_sec);

        // Get the width of the string on the screen so we can center it
        // Returns 0 if width was successfully obtained
        if (!tumGetTextSize((char *)our_time_string,
                            &our_time_strings_width, NULL))
            tumDrawText(our_time_string,
                        SCREEN_WIDTH / 2 -
                        our_time_strings_width / 2,
                        SCREEN_HEIGHT / 2 - DEFAULT_FONT_SIZE / 2,
                        TUMBlue);

        tumDrawUpdateScreen(); // Refresh the screen to draw string

        // Basic sleep of 1000 milliseconds
        vTaskDelay((TickType_t)10);
    }
}
void vTimerCallback( TimerHandle_t xTimer )
 {
 const uint32_t ulMaxExpiryCountBeforeStopping = 10;
 uint32_t ulCount;

    /* Optionally do something if the pxTimer parameter is NULL. */
    configASSERT( xTimer );

    /* The number of times this timer has expired is saved as the
    timer's ID.  Obtain the count. */
    ulCount = ( uint32_t ) pvTimerGetTimerID( xTimer );

    /* Increment the count, then test to see if the timer has expired
    ulMaxExpiryCountBeforeStopping yet. */
    printf("We're inside the vTimerCallback with %d\n", ulCount);
    ulCount++;

    /* If the timer has expired 10 times then stop it from running. */
    if( ulCount >= ulMaxExpiryCountBeforeStopping )
    {
        /* Do not use a block time if calling a timer API function
        from a timer callback function, as doing so could cause a
        deadlock! */
        xTimerStop( xTimer, 0 );
    }
    else
    {
       /* Store the incremented count back into the timer's ID field
       so it can be read back again the next time this software timer
       expires. */
       vTimerSetTimerID( xTimer, ( void * ) ulCount );
    }
 }
int main(int argc, char *argv[])
{
    char *bin_folder_path = tumUtilGetBinFolderPath(argv[0]);

    printf("Initializing: ");

    if (tumDrawInit(bin_folder_path)) {
        PRINT_ERROR("Failed to initialize drawing");
        goto err_init_drawing;
    }

    if (tumEventInit()) {
        PRINT_ERROR("Failed to initialize events");
        goto err_init_events;
    }

    if (tumSoundInit(bin_folder_path)) {
        PRINT_ERROR("Failed to initialize audio");
        goto err_init_audio;
    }

    ppSem = xSemaphoreCreateMutex(); // Locking mechanism

    buttons.lock = xSemaphoreCreateMutex(); // Locking mechanism
    if (!buttons.lock) {
        PRINT_ERROR("Failed to create buttons lock");
        goto err_buttons_lock;
    }

    int x;
    for( x = 0; x < NUM_TIMERS; x++ )
     {
         xTimers[ x ] = xTimerCreate
                   ( /* Just a text name, not used by the RTOS
                     kernel. */
                     "Timer",
                     /* The timer period in ticks, must be
                     greater than 0. */
                     (TickType_t) ((x+1)*1000*portTICK_PERIOD_MS),
                     /* The timers will auto-reload themselves
                     when they expire. */
                     pdTRUE,
                     /* The ID is used to store a count of the
                     number of times the timer has expired, which
                     is initialised to 0. */
                     ( void * ) 0,
                     /* Each timer calls the same callback when
                     it expires. */
                     vTimerCallback
                   );

         if( xTimers[ x ] == NULL )
         {
             /* The timer was not created. */
         }
         else
         {
             /* Start the timer.  No block time is specified, and
             even if one was it would be ignored because the RTOS
             scheduler has not yet been started. */
             if( xTimerStart( xTimers[ x ], 0 ) != pdPASS )
             {
                 /* The timer could not be set into the Active
                 state. */
             }
         }
     }

    if (xTaskCreate(vDemoTask, "DemoTask", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY, &DemoTask) != pdPASS) {
        goto err_demotask;
    }

    if (xTaskCreate(vFakeKeyISRFireTask, "FakeKeyISRTask", mainGENERIC_STACK_SIZE * 2,
		    NULL, mainMAX_PRIORITY, &FakeKeyISRTask) != pdPASS) {
	    goto err_demotask;
    }    

    if (xTaskCreate(vMyTask, "myTask_1", mainGENERIC_STACK_SIZE * 2,
		    NULL, mainGENERIC_PRIORITY+1, &myTask) != pdPASS) {
	    goto err_demotask;
    }

    if (xTaskCreate(vMyTask, "myTask_2", mainGENERIC_STACK_SIZE * 2,
		    NULL, mainGENERIC_PRIORITY+1, &myTask2) != pdPASS) {
	    goto err_demotask;
    }

    if (xTaskCreate(vPingPongTask, "vPingTask", mainGENERIC_STACK_SIZE * 2,
		    "PING", mainGENERIC_PRIORITY+1, &pingTask) != pdPASS) {
	    goto err_demotask;
    }

    if (xTaskCreate(vPingPongTask, "vPongTask", mainGENERIC_STACK_SIZE * 2,
		    "PONG", mainGENERIC_PRIORITY+1, &pongTask) != pdPASS) {
	    goto err_demotask;
    }

    vTaskStartScheduler();

    return EXIT_SUCCESS;

err_demotask:
    vSemaphoreDelete(buttons.lock);
err_buttons_lock:
    tumSoundExit();
err_init_audio:
    tumEventExit();
err_init_events:
    tumDrawExit();
err_init_drawing:
    return EXIT_FAILURE;
}

// cppcheck-suppress unusedFunction
__attribute__((unused)) void vMainQueueSendPassed(void)
{
    /* This is just an example implementation of the "queue send" trace hook. */
}

// cppcheck-suppress unusedFunction
__attribute__((unused)) void vApplicationIdleHook(void)
{
#ifdef __GCC_POSIX__
    struct timespec xTimeToSleep, xTimeSlept;
    /* Makes the process more agreeable when using the Posix simulator. */
    xTimeToSleep.tv_sec = 1;
    xTimeToSleep.tv_nsec = 0;
    nanosleep(&xTimeToSleep, &xTimeSlept);
#endif
}
