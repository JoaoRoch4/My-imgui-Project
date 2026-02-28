#pragma once

#include "imgui.h"
#include <string>
#include <cstdio>

// Helper to debug emoji font loading
class EmojiDebugHelper {
public:
    // Check if emoji font is properly loaded
    static bool CheckEmojiFont() {
        ImGuiIO& io = ImGui::GetIO();

        // Check if we have fonts loaded
        if (!io.Fonts || io.Fonts->Fonts.Size == 0) {
            printf("ERROR: No fonts loaded at all!\n");
            return false;
        }

        printf("=== Font Debug Info ===\n");
        printf("Total fonts loaded: %d\n", io.Fonts->Fonts.Size);
        printf("========================\n");
        return io.Fonts->Fonts.Size > 0;
    }

    // Print helpful debug message
    static void PrintEmojiStatus(bool emoji_loaded) {
        if (!emoji_loaded) {
            printf("\n");
            printf("⚠️  WARNING: No emoji font loaded!\n");
            printf("    The emoji characters will display as ??? \n");
            printf("\n");
            printf("To fix this:\n");
            printf("1. Download: Noto Color Emoji from Google Fonts\n");
            printf("2. Place in: D:\\imgui-docking\\misc\\fonts\\NotoColorEmoji.ttf\n");
            printf("3. Restart the application\n");
            printf("\n");
            printf("Download link:\n");
            printf("https://fonts.google.com/noto/specimen/Noto+Color+Emoji\n");
            printf("\n");
        } else {
            printf("✓ Emoji font loaded successfully!\n");
        }
    }
};
