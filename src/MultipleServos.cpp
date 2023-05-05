#include <Servo.h>

#define NUMBER_OF_SERVO 3
static const int servosPins[NUMBER_OF_SERVO] = {25, 33, 32};

Servo servos[NUMBER_OF_SERVO];

int Amax = 180;
int Amin = 0;

int fingerTestCount = 0;
int waitingTime = 5;

void initServo()
{
    Serial.println("Initializing Servo");
    for (int i = 0; i < NUMBER_OF_SERVO; ++i)
    {
        if (!servos[i].attach(servosPins[i], Servo::CHANNEL_NOT_ATTACHED, 0, 180, 500, 2400))
        {
            Serial.print("Servo ");
            Serial.print(i);
            Serial.println("attach error");
        }
    }
}
void testFinger(int finger)
{

    for (int posDegrees = Amin; posDegrees <= Amax; posDegrees++)
    {
        servos[finger].write(posDegrees);
        // Serial.println(posDegrees);
        vTaskDelay(waitingTime);
    }

    for (int posDegrees = Amax; posDegrees >= Amin; posDegrees--)
    {
        servos[finger].write(posDegrees);
        // Serial.println(posDegrees);
        vTaskDelay(waitingTime);
    }
}
void testFinger()
{
    Serial.println("finger test: " + String(fingerTestCount));
    for (int posDegrees = Amin; posDegrees <= Amax; posDegrees++)
    {
        for (int i = 0; i < NUMBER_OF_SERVO; i++)
        {
            servos[i].write(posDegrees);
        }

        // Serial.println(posDegrees);
        vTaskDelay(waitingTime);
    }

    for (int posDegrees = Amax; posDegrees >= Amin; posDegrees--)
    {
        for (int i = 0; i < NUMBER_OF_SERVO; i++)
        {
            servos[i].write(posDegrees);
        }
        // Serial.println(posDegrees);
        vTaskDelay(waitingTime);
    }
    fingerTestCount++;
}
void setup()
{
    Serial.begin(115200);
    initServo();
}

void loop()
{
    testFinger();
}