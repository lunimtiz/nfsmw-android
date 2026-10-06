#include <jni.h>

#include <string>

#include <rex/cvar.h>
#include <rex/input/sdl/sdl_input_driver.h>

#include "../../../../../app/src/nfsmw_diagnostico.h"

extern "C" JNIEXPORT void JNICALL
Java_com_nfsmw_android_TouchControlsView_nativeSetTouchState(JNIEnv*, jclass, jint buttons, jint left_x,
                                                             jint left_y, jint right_x, jint right_y,
                                                             jint left_trigger, jint right_trigger) {
  rex_sdl_set_touch_gamepad_state(static_cast<uint16_t>(buttons), static_cast<int16_t>(left_x),
                                  static_cast<int16_t>(left_y), static_cast<int16_t>(right_x),
                                  static_cast<int16_t>(right_y), static_cast<uint8_t>(left_trigger),
                                  static_cast<uint8_t>(right_trigger));
}

// Screen mode, live: the presenter reads these cvars on every frame (presenter.cpp).
//   stretch = true:  the game image fills the whole phone screen.
//   stretch = false: 16:9 with bars at the sides, as the game was made.
extern "C" JNIEXPORT void JNICALL
Java_com_nfsmw_android_GameActivity_nativeSetStretch(JNIEnv*, jclass, jboolean stretch) {
  rex::cvar::SetFlagByName("present_letterbox", stretch ? "false" : "true");
  if (stretch) {
    // No overscan crop: all of the image, stretched to the screen.
    rex::cvar::SetFlagByName("present_safe_area_x", "100");
    rex::cvar::SetFlagByName("present_safe_area_y", "100");
  }
}

// Wide diagnostic mode (the DIAG button of the pad): while it is on, the renderer's frame trace runs every few
// frames and goes to a file in `dir` (see app/src/nfsmw_diagnostico.h).
extern "C" JNIEXPORT void JNICALL
Java_com_nfsmw_android_TouchControlsView_nativeSetDiagnostic(JNIEnv* env, jclass, jboolean on, jstring dir) {
  const char* chars = env->GetStringUTFChars(dir, nullptr);
  const std::string folder = chars ? chars : "";
  if (chars) {
    env->ReleaseStringUTFChars(dir, chars);
  }
  nfsmw::diag::Establecer(on, folder);
}
