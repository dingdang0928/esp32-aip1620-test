idf_component_register(
    SRCS
        "main.c"
        "./App/App_ClockDisplay.c"
        "./App/APP_DisplayAIP1620.c"
        "./App/App_AIP1620PowerTest.c"
        "./App/App_SleepRecord.c"
        "./App/App_SleepRecordUartTest.c"
        "./Inf/Inf_AIP1620.c"
        "./Inf/Inf_RGB.c"
        "./Common/Com_Debug.c"
        "./Inf/Inf_RTC.c"
        "./Inf/Inf_SleepStorage.c"
        "./Dri/Dri_BLE.c"
        "./Dri/Dri_NVS.c"
    INCLUDE_DIRS
        "."
        "./App"
        "./Common"
        "./Dri"
        "./Inf"
        "./Protocol"


)
