
#include "pch.h"
#include "Utils.hpp"

#define MAX_TEXT_BUFFER_LENGTH 512

static void LogMessage(int level, const char *msg, va_list args)
{
    time_t rawTime;
    struct tm *timeInfo;
    char timeBuffer[80];

    time(&rawTime);
    timeInfo = localtime(&rawTime);

    strftime(timeBuffer, sizeof(timeBuffer), "[%H:%M:%S]", timeInfo);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);

    switch (level)
    {
    case 0:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s%s", timeBuffer, buffer);
        break;
    case 1:
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s%s", timeBuffer, buffer);
        break;
    case 2:
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s%s", timeBuffer, buffer);
        break;
    }
}

void LogError(const char *msg, ...)
{

    va_list args;
    va_start(args, msg);
    LogMessage(1, msg, args);
    va_end(args);
}

void LogWarning(const char *msg, ...)
{

    va_list args;
    va_start(args, msg);
    LogMessage(2, msg, args);
    va_end(args);
}

void LogInfo(const char *msg, ...)
{
    va_list args;
    va_start(args, msg);
    LogMessage(0, msg, args);
    va_end(args);
}
