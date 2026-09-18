idf_component_register(SRCS "main.cpp" "chi_walk.cpp" "chi_actions.cpp"
                            "chi_game_actions.cpp" "chi_fighting_actions.cpp"
                            "arena_backgrounds.cpp"
                            "ui_assets.cpp"
                       INCLUDE_DIRS "." "../include"
                       REQUIRES esp_lcd esp_driver_i2s driver human_face_detect)
