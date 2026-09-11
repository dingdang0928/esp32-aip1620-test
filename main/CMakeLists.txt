idf_component_register(
    SRCS
        "main.c"
        "./App/App_ClockDisplay.c"
        "./Inf/Inf_AIP1620.c"
        "./Inf/Inf_RGB.c"
        "./Common/Com_Debug.c"
        "./Inf/Inf_RTC.c"

    INCLUDE_DIRS
        "."
        "./App"
        "./Common"
        "./Dri"
        "./Inf"
        "./Protocol"


)
