#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(AppEventDrivenFsm);

/* List of events */
#define EVENT_ANY          (uint32_t)(0xFFFFFFFF)
#define EVENT_BUTTON_PRESS (uint32_t)(BIT(0))

/* Used to facilitate indexing the states[] list */
typedef enum {
  STATE_0 = 0,
  STATE_1,
  STATE_MAX,
} state_t;

/* State machine global context (contains SMF context + user data) */
typedef struct {
  /* This must be first */
  struct smf_ctx ctx;
  /* In our case, user data is the event kernel object and a 32 bits variable to store events */
  struct k_event kEvent;
  uint32_t events;
  /* Other state specific data add here */
} state_machine_t;

static void appThreadHandler(void);
static void state0EventsHandler(void *machine);
static void state1EventsHandler(void *machine);
static void setupButton(const struct gpio_dt_spec *buttonGpio, struct gpio_callback *callbackData);
static void onButtonPressCallback(const struct device *dev, struct gpio_callback *cb, uint32_t pins);

/* Create thread */
K_THREAD_DEFINE(appThread, 1024, appThreadHandler, NULL, NULL, NULL, 7, 0, 0);

/* Get button device from DTS */
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw0), gpios, {0});

/* List of states */
static const struct smf_state states[] = {
  [STATE_0] = SMF_CREATE_STATE(NULL, state0EventsHandler, NULL),
  [STATE_1] = SMF_CREATE_STATE(NULL, state1EventsHandler, NULL),
};

/* Global state machine */
static state_machine_t stateMachine = {0};

/* Button GPIO callback data */
static struct gpio_callback buttonCallbackData = {0};

static void appThreadHandler(void) {
  int ret = 0;

  /* Configure button GPIO and interrupt */
  setupButton(&button, &buttonCallbackData);

  /* Initialize the event kernel object inside the state machine */
  k_event_init(&stateMachine.kEvent);

  /* Set initial state for the state machine */
  smf_set_initial(SMF_CTX(&stateMachine), &states[STATE_0]);

  /* Run the state machine */
  while(true) {
    /* Block forever until any event is detected */
    stateMachine.events = k_event_wait(&stateMachine.kEvent, EVENT_ANY, true, K_FOREVER);
    /* Runs one iteration of a state machine (including any parent states) */
    ret = smf_run_state(SMF_CTX(&stateMachine));
    /* State machine terminates if a non-zero value is returned */
    if (ret) {
      /* Handle return code and terminate state machine */
      break;
    }
  }
}

static void state0EventsHandler(void *machine) {
  /* Filter on events that we need in STATE 0 */
  if (stateMachine.events & EVENT_BUTTON_PRESS) {
    /* Change state on button press event */
    smf_set_state(SMF_CTX(&stateMachine), &states[STATE_1]);
  }
}

static void state1EventsHandler(void *machine) {
  /* Filter on events that we need in STATE 1 */
  if (stateMachine.events & EVENT_BUTTON_PRESS) {
    /* Change state on button press event */
    smf_set_state(SMF_CTX(&stateMachine), &states[STATE_0]);
  }
}

static void setupButton(const struct gpio_dt_spec *buttonGpio, struct gpio_callback *callbackData) {
  int ret;

  /* Setup button GPIO and interrupt */
  if (!gpio_is_ready_dt(buttonGpio)) {
    LOG_ERR("Error: button device %s is not ready", button.port->name);
    return;
  }
  ret = gpio_pin_configure_dt(buttonGpio, GPIO_INPUT);
  if (ret != 0) {
    LOG_ERR("Error %d: failed to configure %s pin %d", ret, button.port->name, button.pin);
    return;
  }
  ret = gpio_pin_interrupt_configure_dt(buttonGpio, GPIO_INT_EDGE_TO_ACTIVE);
  if (ret != 0) {
    LOG_ERR("Error %d: failed to configure interrupt on %s pin %d", ret, button.port->name, button.pin);
    return;
  }
  gpio_init_callback(callbackData, onButtonPressCallback, BIT(button.pin));
  gpio_add_callback(button.port, callbackData);
}

static void onButtonPressCallback(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
  /* Generate button press event */
  k_event_post(&stateMachine.kEvent, EVENT_BUTTON_PRESS);
}
