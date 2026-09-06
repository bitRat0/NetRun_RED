#include <Arduino.h>

#include "app/App.h"

namespace
{
App app;
}

void setup()
{
    Serial.begin(115200);
    app.begin();
}

void loop()
{
    app.update();
}
