#pragma once
#include <Arduino.h>

struct FobData
{
    String userID;
    int fobID;
    int ttl;
    FobData *next;
};
