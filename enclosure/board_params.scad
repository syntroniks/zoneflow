// GENERATED from hardware/mechanical.yaml by enclosure/tools/gen_params.py.
// Do not edit. Change the YAML and run `make` in enclosure/.

board_outline = [
    [0, 0],
    [114, 0],
    [114, 100],
    [0, 100],
];
board_w = 114;
board_h = 100;
board_t = 1.6;
board_edge_keepout = 1;
z_fascia_outer = 12.5;
z_fascia_inner = 10.5;
z_cover_inner = 13;
bottom_clearance = 18;
z_panel_front = 11;
z_panel_back = 8.95;
panel_pad = 0.1;
deck_t = 1.2;
header_face_y = -2;
top_gap = 10;
holes = [
    ["A", 5.5, 95, 3.4, "screw", 9, 10],
    ["B", 70, 95, 3.4, "screw", 9, 10],
    ["C", 4.5, 12, 3.4, "screw", 7, 10],
    ["D", 109.5, 12, 3.4, "screw", 7, 10],
    ["E", 5, 61, 0, "foot", 6, 0],
    ["F", 70, 61, 0, "foot", 6, 0],
    ["G", 86, 64, 0, "post", 0, 7],
];
disp_c = [35, 78];
disp_aa = [40.8, 30.6];
disp_origin = [11.96, 59.9];
disp_outline = [51.8, 36.2];
disp_window_margin = 0.6;
disp_fpc_root_y = [67.2, 88.65];
j7_c = [60.5, 78.43];
j7_entry_x = 63;
enc_c = [85, 78];
enc_shaft_top_z = 24.5;
enc_bushing_d = 8.975;
enc_bushing_top_z = 14.5;
enc_shaft_d = 5.975;
enc_flat_across = 4.45;
enc_flat_len = 7;
enc_travel = 1.5;
btn_c = [[22, 51.5], [48, 51.5]];
btn_top_z = 10;
btn_travel = 0.25;
btn_act_d = 3.5;
led_c = [
    [12, 42],
    [26, 42],
    [35.5, 42],
    [45, 42],
    [54.5, 42],
    [64, 42],
    [73.5, 42],
    [83, 42],
    [92.5, 42],
    [102, 42],
];
led_label = ["", "M", "1", "2", "3", "4", "5", "6", "7", "8"];
led_h = 0.8;
terminals = [
    ["J1", 15.5, 23.92],
    ["J2", 27.92, 55.39],
    ["J3", 55.99, 72.03],
    ["J4", 76.03, 103.5],
];
terminal_labels = ["24V~", "24V~", "COM", "COM", "MV", "1", "2", "3", "4", "5", "6", "7", "8", "5V", "FLOW", "GND", "RAIN", "GND", "AUX", "GND"];
terminal_pin_x = [17.805, 21.615, 30.225, 34.035, 37.845, 41.655, 45.465, 49.275, 53.085, 58.295, 62.105, 65.915, 69.725, 78.335, 82.145, 85.955, 89.765, 93.575, 97.385, 101.195];
header_h = 7;
plug_top_z = 11.6;
plug_len = 8.9;
usb_x = 8;
usb_z = 1.63;
qwiic_x = 109.25;
antenna_rect = [94, 100, 112, 106];
antenna_clearance = 15;
part_prims = [
    ["DS1", "display", "top", "box", 37.86, 78, 51.8, 36.2, 8.95, 11],
    ["DS1-FPC", "display", "top", "box", 67, 77.925, 6.4, 21.65, 4, 10],
    ["DS1-FPC", "display", "top", "box", 66.5, 78.43, 7.4, 6.9, 0.6, 4],
    ["J7", "board", "top", "box", 60.5, 78.43, 5, 9.6, 0, 2],
    ["SW3", "board", "top", "box", 85, 78, 15, 13.5, 0, 4.5],
    ["SW3", "board", "top", "box", 85, 78, 8.5, 18, 0, 1.2],
    ["SW3", "board", "top", "cyl", 85, 78, 8.975, 0, 4.5, 14.5],
    ["SW3", "board", "top", "cyl", 85, 78, 5.975, 0, 14.5, 24.5],
    ["SW4", "board", "top", "box", 22, 51.5, 8.6, 6, 0, 3.3],
    ["SW4", "board", "top", "cyl", 22, 51.5, 3.5, 0, 3.3, 10],
    ["SW5", "board", "top", "box", 48, 51.5, 8.6, 6, 0, 3.3],
    ["SW5", "board", "top", "cyl", 48, 51.5, 3.5, 0, 3.3, 10],
    ["D6", "board", "top", "box", 12, 42, 1.6, 0.8, 0, 0.8],
    ["D902", "board", "top", "box", 26, 42, 1.6, 0.8, 0, 0.8],
    ["D102", "board", "top", "box", 35.5, 42, 1.6, 0.8, 0, 0.8],
    ["D202", "board", "top", "box", 45, 42, 1.6, 0.8, 0, 0.8],
    ["D302", "board", "top", "box", 54.5, 42, 1.6, 0.8, 0, 0.8],
    ["D402", "board", "top", "box", 64, 42, 1.6, 0.8, 0, 0.8],
    ["D502", "board", "top", "box", 73.5, 42, 1.6, 0.8, 0, 0.8],
    ["D602", "board", "top", "box", 83, 42, 1.6, 0.8, 0, 0.8],
    ["D702", "board", "top", "box", 92.5, 42, 1.6, 0.8, 0, 0.8],
    ["D802", "board", "top", "box", 102, 42, 1.6, 0.8, 0, 0.8],
    ["J5", "board", "top", "box", 8, 2.675, 8.94, 7.35, 0, 3.26],
    ["J1", "board", "top", "box", 19.71, 2.6, 8.42, 9.2, 0, 7],
    ["J2", "board", "top", "box", 41.655, 2.6, 27.47, 9.2, 0, 7],
    ["J3", "board", "top", "box", 64.01, 2.6, 16.04, 9.2, 0, 7],
    ["J4", "board", "top", "box", 89.765, 2.6, 27.47, 9.2, 0, 7],
    ["J6", "board", "top", "box", 109.25, 1.625, 6, 4.25, 0, 2.95],
    ["J1-plug", "mate", "top", "box", 19.71, -6.45, 8.42, 8.9, 0, 11.6],
    ["J2-plug", "mate", "top", "box", 41.655, -6.45, 27.47, 8.9, 0, 11.6],
    ["J3-plug", "mate", "top", "box", 64.01, -6.45, 16.04, 8.9, 0, 11.6],
    ["J4-plug", "mate", "top", "box", 89.765, -6.45, 27.47, 8.9, 0, 11.6],
    ["U4", "board", "top", "box", 103, 93.25, 18, 25.5, 0, 3.25],
    ["BT1", "board", "top", "box", 93, 54, 31.3, 16, 0, 5.5],
    ["C1", "board", "bottom", "box", 16, 32, 17, 17, 0, 17],
    ["K1", "board", "bottom", "box", 42, 22, 20.5, 7.2, 0, 15.3],
];
zones = [
    ["T_GENERAL", "top", 0, 0, 114, 100, 9],
    ["T_CARRIER", "top", 0, 57.5, 74.5, 100, 6.5],
    ["T_FPC", "top", 58, 66.5, 72, 89.5, 0],
    ["T_BTN_BACK", "top", 11.5, 45, 32.5, 58, 7],
    ["T_BTN_HOME", "top", 37.5, 45, 58.5, 58, 7],
    ["T_LIGHTPIPE", "top", 9.3, 39.3, 104.7, 44.7, 1],
    ["T_ANTENNA", "top", 79, 85, 114, 100, 3.5],
    ["T_EDGE_LEFT", "top", 0, 0, 1, 100, 0],
    ["T_EDGE_RIGHT", "top", 113, 0, 114, 100, 0],
    ["B_GENERAL", "bottom", 0, 0, 114, 100, 17],
    ["B_KEYHOLE_L", "bottom", 10.5, 70, 28.5, 94, 10],
    ["B_KEYHOLE_R", "bottom", 85.5, 70, 103.5, 94, 10],
    ["B_ANTENNA", "bottom", 79, 85, 114, 100, 3.5],
];
