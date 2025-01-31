SET(SOURCES components/ui_comp_labelpricehigh.c
    components/ui_comp_labeldate.c
    components/ui_comp_labelpricelow.c
    components/ui_comp.c
    screens/ui_loading.c
    screens/ui_wifiProv.c
    screens/ui_ticker.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_images_logo_png.c
    images/ui_img_1283391855.c
    images/ui_img_213572400.c
    images/ui_img_ethnew_png.c
    images/ui_img_images_btc_png.c
    images/ui_img_376556310.c
    images/ui_img_msg_png.c
    fonts/ui_font_Font100.c
    fonts/ui_font_Font25.c
    fonts/ui_font_Font38.c)

add_library(ui ${SOURCES})
