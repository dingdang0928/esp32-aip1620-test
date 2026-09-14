idf_component_register(
    SRCS
        "main.c"
        "./App/App_ClockDisplay.c"
        "./App/APP_DisplayAIP1620.c"
        "./App/App_AIP1620PowerTest.c"
        "./Inf/Inf_AIP1620.c"
        "./Inf/Inf_RGB.c"
        "./Common/Com_Debug.c"
        "./Inf/Inf_RTC.c"
        "./Dri/Dri_BLE.c"
    INCLUDE_DIRS
        "."
        "./App"
        "./Common"
        "./Dri"
        "./Inf"
        "./Protocol"


)
