package org.cutwire.drift;

import android.app.Activity;
import android.content.Context;
import android.media.AudioAttributes;
import android.os.Build;
import android.os.VibrationAttributes;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.view.HapticFeedbackConstants;
import android.view.View;
import android.view.Window;

// Touch feedback for the editor. Two routes, tried in that order:
//
//   1. View.performHapticFeedback, which renders the OEM's own tuned effect for the interaction
//      and needs no permission. It is the better feel wherever it works.
//   2. Vibrator, when the first route declines. API 30+ prefers VibrationEffect.Composition
//      primitives (the actuator's own click/tick/rise) and falls back to predefined effects.
//
// The fallback is the whole point. performHapticFeedback's one-argument overload returns false and
// does nothing whenever the system-wide touch-feedback setting is off ("Vibration & haptics →
// Touch feedback", off by default on a fair number of OEM skins), or whenever the decor view has no
// attach info yet. There is no longer a way around that from the view API — FLAG_IGNORE_GLOBAL_SETTING
// has been ignored since API 33 — so an app that only ever calls route 1 is silent on those devices
// with no indication why. That was the bug: every call site was firing correctly and nothing moved.
//
// Route 2 therefore answers to Drift's own switch (Settings → Interface → Haptic feedback) rather
// than to the system touch-feedback setting: the C++ side never calls in here at all when that
// switch is off. The system setting still wins wherever route 1 succeeds, which is the common case.
//
// perform() must be called on the Android UI thread — performHapticFeedback goes through the view
// hierarchy, which is not thread-safe, and Qt's GUI thread is a different thread. The C++ side
// (drift::Haptics) hops before calling, the same way AudioFocus does. hasVibrator() is the
// exception: it touches no view and drift::Haptics::isSupported() asks it straight from the Qt GUI
// thread, which is why the vibrator lookup they share is synchronized.
public class Haptics
{
    // Intents, not effects: the call site says what happened and the mapping below decides what
    // that feels like on the device it is running on. Kept in step with drift::Haptics::Effect.
    public static final int SELECT = 0;
    public static final int SNAP = 1;
    public static final int PICK_UP = 2;
    public static final int DROP = 3;
    public static final int BOUNDARY = 4;
    public static final int DETENT = 5;
    public static final int CONFIRM = 6;
    public static final int PRESS = 7;
    public static final int TOGGLE_ON = 8;
    public static final int TOGGLE_OFF = 9;
    public static final int SUCCESS = 10;
    public static final int ERROR = 11;

    // Process-wide and immutable once resolved, unlike the decor view below. Resolved lazily
    // because the first call happens well after the activity exists. Guarded by the lock on this
    // class: the two entry points that reach it do not run on the same thread.
    private static Vibrator sVibrator;
    private static boolean sVibratorResolved;

    // API 34 added constants that name these interactions outright, and a device with a modern
    // actuator renders them distinctly. Below that they collapse onto the handful that have been
    // around since API 21 — a tick is still the right shape, it just stops being a *specific* tick.
    // Nothing here ever passes a constant the running platform does not know: an unrecognised value
    // is not a no-op, it falls through to whatever the OEM mapped that integer to.
    private static int constantFor(int intent)
    {
        final int sdk = Build.VERSION.SDK_INT;
        switch (intent) {
        case SELECT:
            return HapticFeedbackConstants.CLOCK_TICK;
        case PRESS:
            return HapticFeedbackConstants.CONTEXT_CLICK;
        case SNAP:
        case DETENT:
            return sdk >= 34 ? HapticFeedbackConstants.SEGMENT_TICK
                             : HapticFeedbackConstants.CLOCK_TICK;
        case PICK_UP:
            // LONG_PRESS on every level, including the ones that have DRAG_START. DRAG_START is
            // deliberately faint — it is meant to sit under a drag that has already started moving,
            // not to announce one — and the moment a clip becomes draggable is the single most
            // important beat in the gesture: nothing on screen can report it ahead of the user's
            // own movement. LONG_PRESS is the effect every device renders firmly.
            return HapticFeedbackConstants.LONG_PRESS;
        case DROP:
            return sdk >= 30 ? HapticFeedbackConstants.GESTURE_END
                             : HapticFeedbackConstants.CLOCK_TICK;
        case BOUNDARY:
        case ERROR:
            // REJECT is the "that did not happen" effect — the right answer for an edge that
            // refuses to move, and distinct enough from SNAP that the two do not blur together
            // during a trim, which is the one drag that produces both.
            return sdk >= 30 ? HapticFeedbackConstants.REJECT
                             : HapticFeedbackConstants.CLOCK_TICK;
        case CONFIRM:
            return sdk >= 30 ? HapticFeedbackConstants.CONFIRM
                             : HapticFeedbackConstants.VIRTUAL_KEY;
        case TOGGLE_ON:
            return sdk >= 34 ? HapticFeedbackConstants.TOGGLE_ON
                             : (sdk >= 30 ? HapticFeedbackConstants.CONFIRM
                                          : HapticFeedbackConstants.VIRTUAL_KEY);
        case TOGGLE_OFF:
            return sdk >= 34 ? HapticFeedbackConstants.TOGGLE_OFF
                             : HapticFeedbackConstants.CLOCK_TICK;
        case SUCCESS:
            // CONFIRM is the platform's "this completed" effect. A two-beat success lives on the
            // vibrator fallback below; the view API has no richer success constant.
            return sdk >= 30 ? HapticFeedbackConstants.CONFIRM
                             : HapticFeedbackConstants.VIRTUAL_KEY;
        default:
            return HapticFeedbackConstants.CLOCK_TICK;
        }
    }

    // Context rather than Activity, matching AudioFocus. What Qt hands across is whatever
    // QAndroidApplication::context() resolves to, and that is only guaranteed to be a Context —
    // typing the parameter as Activity would make an unexpected one a crash inside getWindow()
    // instead of a route that quietly falls through to the vibrator.
    public static void perform(Context context, int intent)
    {
        if (context == null)
            return;
        if (context instanceof Activity) {
            Window window = ((Activity) context).getWindow();
            // Fetched per call rather than cached in a static: the decor view is replaced when the
            // activity's content is, and a stale reference is a view with no attach info, which
            // swallows the feedback silently. Two JNI lookups against a rate limiter that lets at
            // most ~25 of these through a second is not worth a staleness bug.
            View view = window != null ? window.getDecorView() : null;
            // The one-argument overload honours both View.isHapticFeedbackEnabled and the
            // system-wide touch feedback setting, and reports which way it went. Do not reach for
            // the FLAG_IGNORE_* overload to "make it reliable" — the platform stopped honouring it.
            if (view != null && view.performHapticFeedback(constantFor(intent)))
                return;
        }
        vibrate(context, intent);
    }

    // --- Fallback ---------------------------------------------------------------------------

    private static synchronized Vibrator vibrator(Context context)
    {
        if (sVibratorResolved)
            return sVibrator;
        sVibratorResolved = true;
        if (Build.VERSION.SDK_INT >= 31) {
            VibratorManager manager = context.getSystemService(VibratorManager.class);
            sVibrator = manager != null ? manager.getDefaultVibrator() : null;
        } else {
            sVibrator = context.getSystemService(Vibrator.class);
        }
        if (sVibrator != null && !sVibrator.hasVibrator())
            sVibrator = null;
        return sVibrator;
    }

    // The same intents again, this time as motor commands. Composition primitives (API 30) are
    // preferred wherever the actuator advertises them, because they are the same vocabulary the
    // OEM uses for its own keyboard and switches. Predefined effects cover API 29; minSdk is 28,
    // so exactly one level needs the hand-rolled durations underneath.
    private static boolean primitiveOk(Vibrator vibrator, int primitive)
    {
        return Build.VERSION.SDK_INT >= 30 && vibrator != null
               && vibrator.areAllPrimitivesSupported(primitive);
    }

    private static VibrationEffect compositionFor(Vibrator vibrator, int intent)
    {
        if (Build.VERSION.SDK_INT < 30 || vibrator == null)
            return null;
        try {
            VibrationEffect.Composition c = VibrationEffect.startComposition();
            switch (intent) {
            case PICK_UP:
                if (!primitiveOk(vibrator, VibrationEffect.Composition.PRIMITIVE_QUICK_RISE))
                    return null;
                c.addPrimitive(VibrationEffect.Composition.PRIMITIVE_QUICK_RISE, 1.0f);
                return c.compose();
            case TOGGLE_ON:
                if (!primitiveOk(vibrator, VibrationEffect.Composition.PRIMITIVE_CLICK))
                    return null;
                if (primitiveOk(vibrator, VibrationEffect.Composition.PRIMITIVE_QUICK_RISE))
                    c.addPrimitive(VibrationEffect.Composition.PRIMITIVE_QUICK_RISE, 0.8f, 0);
                c.addPrimitive(VibrationEffect.Composition.PRIMITIVE_CLICK, 1.0f, 20);
                return c.compose();
            case TOGGLE_OFF:
                if (!primitiveOk(vibrator, VibrationEffect.Composition.PRIMITIVE_QUICK_FALL))
                    return null;
                c.addPrimitive(VibrationEffect.Composition.PRIMITIVE_QUICK_FALL, 0.7f);
                return c.compose();
            case SUCCESS:
                if (!primitiveOk(vibrator, VibrationEffect.Composition.PRIMITIVE_CLICK))
                    return null;
                c.addPrimitive(VibrationEffect.Composition.PRIMITIVE_CLICK, 1.0f, 0);
                if (primitiveOk(vibrator, VibrationEffect.Composition.PRIMITIVE_TICK))
                    c.addPrimitive(VibrationEffect.Composition.PRIMITIVE_TICK, 0.8f, 70);
                return c.compose();
            case ERROR:
                if (!primitiveOk(vibrator, VibrationEffect.Composition.PRIMITIVE_SLOW_RISE))
                    return null;
                c.addPrimitive(VibrationEffect.Composition.PRIMITIVE_SLOW_RISE, 0.6f, 0);
                if (primitiveOk(vibrator, VibrationEffect.Composition.PRIMITIVE_QUICK_FALL))
                    c.addPrimitive(VibrationEffect.Composition.PRIMITIVE_QUICK_FALL, 1.0f, 30);
                return c.compose();
            default:
                return null;
            }
        } catch (IllegalArgumentException ignored) {
            return null;
        }
    }

    private static VibrationEffect effectFor(Vibrator vibrator, int intent)
    {
        VibrationEffect composed = compositionFor(vibrator, intent);
        if (composed != null)
            return composed;
        if (Build.VERSION.SDK_INT >= 29) {
            switch (intent) {
            case PICK_UP:
            case ERROR:
                return VibrationEffect.createPredefined(VibrationEffect.EFFECT_HEAVY_CLICK);
            case BOUNDARY:
                return VibrationEffect.createPredefined(VibrationEffect.EFFECT_DOUBLE_CLICK);
            case DROP:
            case CONFIRM:
            case SUCCESS:
            case TOGGLE_ON:
            case PRESS:
                return VibrationEffect.createPredefined(VibrationEffect.EFFECT_CLICK);
            default:
                return VibrationEffect.createPredefined(VibrationEffect.EFFECT_TICK);
            }
        }
        switch (intent) {
        case PICK_UP:
        case ERROR:
            return VibrationEffect.createOneShot(28, 255);
        case BOUNDARY:
            // Two knocks with a gap, which is what EFFECT_DOUBLE_CLICK is above.
            return VibrationEffect.createWaveform(new long[] {0, 16, 70, 16},
                                                  new int[] {0, 200, 0, 200}, -1);
        case SUCCESS:
            return VibrationEffect.createWaveform(new long[] {0, 18, 55, 12},
                                                  new int[] {0, 200, 0, 140}, -1);
        case DROP:
        case CONFIRM:
        case TOGGLE_ON:
        case PRESS:
            return VibrationEffect.createOneShot(18, 160);
        default:
            // Ticks ride under a moving finger and stack up over a drag: short and light, or the
            // timeline feels like it is grinding.
            return VibrationEffect.createOneShot(10, 90);
        }
    }

    private static void vibrate(Context context, int intent)
    {
        Vibrator vibrator = vibrator(context);
        if (vibrator == null)
            return;
        VibrationEffect effect = effectFor(vibrator, intent);
        // Usage matters: tagged as touch feedback (or sonification below API 33) the platform
        // routes it as an interface response, so Do Not Disturb and the ringer's own vibrate
        // setting treat it the way they treat a keyboard tick rather than an alarm.
        if (Build.VERSION.SDK_INT >= 33) {
            vibrator.vibrate(effect,
                             VibrationAttributes.createForUsage(VibrationAttributes.USAGE_TOUCH));
        } else {
            vibrator.vibrate(effect,
                             new AudioAttributes.Builder()
                                     .setUsage(AudioAttributes.USAGE_ASSISTANCE_SONIFICATION)
                                     .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                                     .build());
        }
    }

    // True when the device has an actuator at all. Tablets and emulators frequently do not, and a
    // settings switch that controls nothing is worse than no switch.
    public static boolean hasVibrator(Context context)
    {
        if (context == null)
            return false;
        return vibrator(context) != null;
    }
}
