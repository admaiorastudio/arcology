#include <LiquidCrystal_I2C.h>
#include <IRremoteESP8266.h>
#include <EEPROM.h>
#include <IRsend.h>
#include "ir.h"

const int DISPLAY_WIDTH = 16;
const int DISPLAY_HEIGHT = 2;

const int MAX_COCKTAILS = 50;
const int MAX_COCKTAIL_STEPS = 10;
const int MAX_COLORS = 5;

const int INTERVAL_COCKTAIL_STEP = 4000;
const int INTERVAL_BACK_TO_MAINMENU = 5 * 60 * 1000;
const int INTERVAL_SHOW_INFO = 10 * 60 * 1000;


const int BUTTON_1 = 0;
const int BUTTON_1_PIN = 12;
const int BUTTON_2 = 1;
const int BUTTON_2_PIN = 13;
const int BUTTON_3 = 2;
const int BUTTON_3_PIN = 0;

const int RELAY_PIN = 14;

const String MENU_HOME = "MENU_HOME";
const String MENU_RECIPES = "MENU_RECIPES";
const String MENU_COLORS = "MENU_COLORS";
const String MENU_RELAY = "MENU_RELAY";

class Trigger {
private:
    uint32_t timer;
    uint32_t interval;
    bool triggered;

public:

    Trigger(int intervalMs) {
        this->timer = 0;
        this->interval = intervalMs;
    }

    void update(uint32_t elapsedMs) {
        this->timer += elapsedMs;
        this->triggered = false;
        if (this->timer > this->interval)
        {
            this->timer = 0;
            this->triggered = true;
        }
    }

    void reset() {
        this->timer = 0;
        this->triggered = false;
    }

    bool isTriggered() {
        return this->triggered;
    }
};

class Display {
private:
    // Construct an LCD object and pass it the 
    // I2C address, width (in characters) and
    // height (in characters). Depending on the
    // Actual device, the IC2 address may change.
    LiquidCrystal_I2C* lcd;
    String buffer[2];

    void clearRow(int row) {
        lcd->setCursor(0, row);
        lcd->print("                ");
    }

public:

    void init() {
        lcd = new LiquidCrystal_I2C(0x27, 5, 4);
        lcd->begin(DISPLAY_WIDTH, DISPLAY_HEIGHT);
        lcd->init();
        lcd->backlight();
        lcd->setCursor(0, 0);
        lcd->clear();
    }

    void clear() {
        lcd->clear();
    }

    void write(int col, int row, String string) {
        if (this->buffer[row] != string)
            this->clearRow(row);
        lcd->setCursor(col, row);
        lcd->print(string);
        this->buffer[row] = string;
    }

    void writeCenter(int row, String string) {
        int col = (DISPLAY_WIDTH - string.length()) / 2;
        this->write(col, row, string);
    }
};

class Button {
private:
    int pin;
    int state; // 0 released 1 pressed
    int oldState;
    bool isLongPressed;
    bool triggerLongPressed;
    Trigger* lpTrigger;

public:

    Button(int pin) {
        this->pin = pin;
        this->state = 0;
        this->oldState = 0;
        this->isLongPressed = false;
        this->triggerLongPressed = true;
        this->lpTrigger = new Trigger(1000);
    }

    void init() {
        pinMode(this->pin, INPUT_PULLUP);

    }

    void update(uint32_t elapsedMs) {
        this->isLongPressed = false;
        this->oldState = this->state;
        this->state = this->getIsDown() ? 1 : 0;
        if (this->getIsDown()
            && this->triggerLongPressed) {
            this->lpTrigger->update(elapsedMs);
            if (this->lpTrigger->isTriggered()) {
                this->isLongPressed = true;
                this->triggerLongPressed = false;
                this->lpTrigger->reset();
            }
        }
        if (this->getIsReleased())
        {
            this->triggerLongPressed = true;
            this->lpTrigger->reset();
        }
    }

    bool getIsPressed() {
        return this->state == 1 && this->state != this->oldState;
    }

    bool getIsLongPressed() {
        return this->isLongPressed;
    }

    bool getIsReleased() {
        return this->state == 0 && this->state != this->oldState;
    }

    bool getIsDown() {
        return digitalRead(this->pin) == 0;
    }

    int getPIN() {
        return this->pin;
    }
};

class Color {
private:
    String name;
    int value;

public:

    Color(String name, int value) {
        this->name = name;
        this->value = value;
    }

    String getName() {
        return this->name;
    }

    int getValue() {
        return this->value;
    }
};

class Cocktail {
private:
    String name;
    String *steps[MAX_COCKTAIL_STEPS];
    int stepsCount;
    int currentStep;
    Trigger* stepTrigger;

    void updateCurrentStep(uint32_t elapsedMs) {
        this->stepTrigger->update(elapsedMs);
        if (this->stepTrigger->isTriggered()) {
            this->currentStep++;
            if (this->currentStep > stepsCount - 1)
                this->currentStep = 0;
        }
    }

public:

    Cocktail(String name, String *steps[], int stepsCount) {
        this->name = name;
        this->stepsCount = stepsCount;
        this->currentStep = 0;
        this->stepTrigger = new Trigger(INTERVAL_COCKTAIL_STEP);
        for (int i = 0; i < stepsCount; i++) {
            if (steps[i] != NULL)
                this->steps[i] = steps[i];
        }
    }

    void update(uint32_t elapsedMs) {
        updateCurrentStep(elapsedMs);
    }

    void draw(Display *display) {
        display->writeCenter(0, this->name);
        String* step = this->steps[this->currentStep];
        display->writeCenter(1, *step);
    }

    void reset() {
        this->currentStep = 0;
        this->stepTrigger->reset();
    }

    String getName() {
        return this->name;
    }
};

class MainMenu {
private:
    String currentSection;
    Cocktail *cocktails[MAX_COCKTAILS];
    int cocktailsCount;
    int selectedCocktail;
    Button *buttons[3];
    IRsend *ir;
    Color *colors[MAX_COLORS];
    int currentColor;
    int relayStatus = 0;
    Trigger* mmTrigger;
    Trigger* infoTrigger;

    bool getButtonIsPressed(int index) {
        return buttons[index]->getIsPressed();
    }

    bool getButtonIsLongPressed(int index) {
        return buttons[index]->getIsLongPressed();
    }

    bool getButtonIsDown(int index) {
        return buttons[index]->getIsDown();
    }

    void addCocktail(String name, String *steps[], int stepsCount) {
        if (this->cocktailsCount < MAX_COCKTAILS - 1) {
            Cocktail* c = new Cocktail(name, steps, stepsCount);
            this->cocktails[this->cocktailsCount] = c;
            this->cocktailsCount++;
        }
    }

    void addGinTonic() {
        const int count = 3;
        String* steps[count];
        steps[0] = new String("1/3 Gin");
        steps[1] = new String("1/3 Tonica");
        steps[2] = new String("1/3 Vermout");
        this->addCocktail("Gin Tonic", steps, count);
    }

    void addVodkaTonic() {
        const int count = 3;
        String* steps[count];
        steps[0] = new String("1/3 Vodka");
        steps[1] = new String("1/3 Tonica");
        steps[2] = new String("1/3 Ketchup");
        this->addCocktail("Vodka Tonic", steps, count);
    }

    void addAddGrog() {
        const int count = 3;
        String* steps[count];
        steps[0] = new String("1/3 Gasolio");
        steps[1] = new String("1/3 Diluente");
        steps[2] = new String("1/3 Acido Solfo");
        this->addCocktail("Grog", steps, count);
    }

    void loadCockatails() {
        this->addGinTonic();
        this->addVodkaTonic();
        this->addAddGrog();
        this->selectedCocktail = 0;
    }

    void loadColors() {
        this->colors[0] = new Color("Red", IR_R);
        this->colors[1] = new Color("Green", IR_G);
        this->colors[2] = new Color("Blue", IR_B);
        this->colors[3] = new Color("Yellow", IR_B13);
        this->colors[4] = new Color("Fucsia", IR_B2);
        this->currentColor = (int)EEPROM.read(0);
    }

    void updateButtons(uint32_t elapsedMs) {
        for (int i = 0; i < 3; i++) {
            this->buttons[i]->update(elapsedMs);
        }
    }

    void updateCocktails(uint32_t elapsedMs) {
        Cocktail* cocktail = getCurrentCocktail();
        cocktail->update(elapsedMs);
    }

    void updateTimers(uint32_t elapsedMs) {
        if(this->currentSection != MENU_HOME)
            this->mmTrigger->update(elapsedMs);
        if (this->currentSection == MENU_HOME)
            this->infoTrigger->update(elapsedMs);
    }

    Cocktail* getCurrentCocktail() {
        Cocktail* cocktail = this->cocktails[this->selectedCocktail];
        return cocktail;
    }

    void drawMainMenu(Display *display) {
        display->writeCenter(0, "Arcology");
        display->writeCenter(1, "Ready");
    }

    void drawCocktail(Display *display) {
        Cocktail* cocktail = this->getCurrentCocktail();
        cocktail->draw(display);
    }

    void drawColor(Display *display) {
        display->writeCenter(0, "Lights Color");
        if (this->currentColor == -1) {
            display->writeCenter(1, "OFF");
        } 
        else {
            Color* color = this->colors[this->currentColor];
            display->writeCenter(1, color->getName());
        }
    }

    void drawRelayStatus(Display *display) {
        display->writeCenter(0, "Reactor State");
        if (this->relayStatus == 0) {
            display->writeCenter(1, "DISABLED");
        }
        else {
            display->writeCenter(1, "ACTIVE");
        }
    }

public:
    
    MainMenu() {
        this->currentSection = MENU_HOME;
        this->selectedCocktail = 0;
        this->cocktailsCount = 0;
        
        this->mmTrigger = new Trigger(INTERVAL_BACK_TO_MAINMENU);
        this->infoTrigger = new Trigger(INTERVAL_SHOW_INFO);
        
        this->ir = new IRsend(16);
    }

    void init() {

        // Reserve bytes
        EEPROM.begin(128);
        delay(50);

        this->selectedCocktail = 0;

        // Init Relay
        pinMode(RELAY_PIN, OUTPUT);
        delay(50);
        
        // Init buttons
        this->buttons[BUTTON_1] = new Button(BUTTON_1_PIN); //D6 (gpio12)
        this->buttons[BUTTON_2] = new Button(BUTTON_2_PIN); // D7
        this->buttons[BUTTON_3] = new Button(BUTTON_3_PIN); // D8
        for (int i = 0; i < 3; i++) {
            this->buttons[i]->init();
            delay(50);
        }

        // Init IR
        this->ir->begin();
        delay(50);
        //this->ir->sendNEC(IR_OFF, 32);
        //delay(100);

        this->loadCockatails();
        this->loadColors();
    }

    void update(uint32_t elapsedMs) {
        this->updateTimers(elapsedMs);
        this->updateButtons(elapsedMs);
        
        if (this->currentSection == MENU_RECIPES) {
            this->updateCocktails(elapsedMs);
        }

        if (this->getButtonIsPressed(BUTTON_1)) {
            if (this->currentSection == MENU_RECIPES) {
                this->selectedCocktail++;
                if (this->selectedCocktail > this->cocktailsCount - 1)
                    this->selectedCocktail = 0;
                Cocktail* cocktail = this->getCurrentCocktail();
                cocktail->reset();
                this->mmTrigger->reset();
            }
            else {
                this->currentSection = MENU_RECIPES;
                this->mmTrigger->reset();
            }
        }

        if (this->getButtonIsPressed(BUTTON_2)) {
            if (this->currentSection == MENU_COLORS) {
                this->currentColor++;
                if (this->currentColor > MAX_COLORS - 1)
                    this->currentColor = 0;

                EEPROM.write(0, (byte)this->currentColor);
                Color *color = this->colors[this->currentColor];
                this->ir->sendNEC(color->getValue(), 32);
                delay(50);
            }
            else {
                this->currentSection = MENU_COLORS;
                this->mmTrigger->reset();
            }
        }

        if (this->getButtonIsLongPressed(BUTTON_2)) {
            this->ir->sendNEC(IR_OFF, 32);
            delay(50);
            Color* color = this->colors[this->currentColor];
            this->ir->sendNEC(color->getValue(), 32);
            delay(50);
        }

        if (this->getButtonIsPressed(BUTTON_3)) {
            if (this->currentSection == MENU_RELAY) {
                this->relayStatus = this->relayStatus == 0 ? 1 : 0;
                digitalWrite(RELAY_PIN, this->relayStatus == 0 ? LOW : HIGH);
                delay(50);
            }
            else {
                this->currentSection = MENU_RELAY;
                this->mmTrigger->reset();
            }
        }

        if (this->mmTrigger->isTriggered()) {
            this->currentSection = MENU_HOME;
        }
    }

    void draw(Display *display) {
        if (this->currentSection == MENU_HOME) {
            drawMainMenu(display);
        }
        else if(this->currentSection == MENU_RECIPES) {
            drawCocktail(display);
        }
        else if (this->currentSection == MENU_COLORS) {
            drawColor(display);
        }
        else if (this->currentSection == MENU_RELAY) {
            drawRelayStatus(display);
        }
    }

    void getDebugInfo() {
        Serial.println("This is the MainMenu");
        Serial.print("Menu selection: ");
        Serial.println(this->currentSection);
        Serial.print("Cocktails count: ");
        Serial.println(this->cocktailsCount);
        Serial.print("Selected cocktail: ");
        Serial.println(this->selectedCocktail);
        Serial.print("Buttons states: ");
        Serial.print("B1:");
        Serial.print(this->getButtonIsPressed(0));
        Serial.print(this->getButtonIsDown(0));
        Serial.print("  B2:");
        Serial.print(this->getButtonIsPressed(1));
        Serial.print(this->getButtonIsDown(1));
        Serial.print("  B3:");
        Serial.print(this->getButtonIsPressed(2));
        Serial.print(this->getButtonIsDown(2));
        Serial.println("");
        Serial.println("");

        //Serial.println("Cocktails: ");
        //for (int i = 0; i < this->cocktailsCount; i++)
        //{
        //    Cocktail* cocktail = this->cocktails[i];
        //    Serial.println(cocktail->getName());
        //}
    }
};

Display *display = new Display();
MainMenu *mainMenu = new MainMenu();

void setup() {

    Serial.begin(9600);

    Serial.println("");
    Serial.println("Booting...");

    display->init();
    display->writeCenter(0, "Booting");

    mainMenu->init();
}

uint32_t lltms = 0;
uint32_t elapsedMs = 0;

void loop() {
    
    elapsedMs = lltms == 0 ? 0 : millis() - lltms;
    lltms = millis();

    // Update
    mainMenu->update(elapsedMs);

    // Draw
    //display->clear();
    mainMenu->draw(display);
    
    // Debug
    //mainMenu->getDebugInfo();
    
    delay(60);
}
