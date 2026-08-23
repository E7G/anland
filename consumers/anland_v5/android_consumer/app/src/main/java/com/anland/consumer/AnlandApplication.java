package com.anland.consumer;

import android.app.Activity;
import android.app.Application;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewTreeObserver;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.Map;
import java.util.WeakHashMap;

/**
 * Installs the optional gamepad without coupling it to Anland's display pipeline.
 *
 * Main/SecondaryActivity get the overlay only when the preference is enabled.
 * SettingsActivity gets a normal-looking home category injected at runtime, so
 * the large existing settings implementation does not need gamepad-specific code.
 */
public final class AnlandApplication extends Application {
    private static final String SETTINGS_ROW_TAG = "anland_virtual_gamepad_settings_row";
    private static final String OVERLAY_TAG = "anland_virtual_gamepad_overlay";

    private final Map<Activity, ViewTreeObserver.OnGlobalLayoutListener> settingsWatchers =
            new WeakHashMap<>();
    private Activity activeDisplay;

    @Override
    public void onCreate() {
        super.onCreate();
        registerActivityLifecycleCallbacks(new ActivityLifecycleCallbacks() {
            @Override
            public void onActivityResumed(Activity activity) {
                if (activity instanceof MainActivity) {
                    activeDisplay = activity;
                    applyGamepadOverlay(activity);
                } else if (activity instanceof SettingsActivity) {
                    installSettingsWatcher(activity);
                }
            }

            @Override
            public void onActivityPaused(Activity activity) {
                if (activity == activeDisplay) {
                    detachGamepadOverlay(activity);
                    GamepadBridge.stop();
                    activeDisplay = null;
                }
            }

            @Override
            public void onActivityDestroyed(Activity activity) {
                removeSettingsWatcher(activity);
                if (activity == activeDisplay) {
                    detachGamepadOverlay(activity);
                    GamepadBridge.stop();
                    activeDisplay = null;
                }
            }

            @Override public void onActivityCreated(Activity a, Bundle b) {}
            @Override public void onActivityStarted(Activity a) {}
            @Override public void onActivityStopped(Activity a) {}
            @Override public void onActivitySaveInstanceState(Activity a, Bundle b) {}
        });
    }

    private void applyGamepadOverlay(Activity activity) {
        SharedPreferences prefs = getSharedPreferences(
                VirtualGamepadView.PREFS_NAME, MODE_PRIVATE);
        boolean enabled = prefs.getBoolean(VirtualGamepadView.KEY_ENABLED, false);
        if (!enabled) {
            detachGamepadOverlay(activity);
            GamepadBridge.stop();
            return;
        }

        ViewGroup content = activity.findViewById(android.R.id.content);
        if (content == null)
            return;

        View existing = content.findViewWithTag(OVERLAY_TAG);
        if (existing == null) {
            VirtualGamepadView overlay = new VirtualGamepadView(activity);
            overlay.setTag(OVERLAY_TAG);
            content.addView(overlay, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT));
            overlay.bringToFront();
        } else {
            existing.bringToFront();
        }

        Intent intent = activity.getIntent();
        String containerName = intent == null ? "" : intent.getStringExtra("window_name");
        GamepadBridge.start(activity, containerName);
    }

    private void detachGamepadOverlay(Activity activity) {
        ViewGroup content = activity.findViewById(android.R.id.content);
        if (content == null)
            return;
        View overlay = content.findViewWithTag(OVERLAY_TAG);
        if (overlay instanceof VirtualGamepadView)
            ((VirtualGamepadView) overlay).neutralize();
        if (overlay != null && overlay.getParent() instanceof ViewGroup)
            ((ViewGroup) overlay.getParent()).removeView(overlay);
    }

    private void installSettingsWatcher(Activity activity) {
        if (settingsWatchers.containsKey(activity))
            return;

        View decor = activity.getWindow().getDecorView();
        ViewTreeObserver.OnGlobalLayoutListener listener =
                () -> maybeInjectSettingsRow(activity);
        settingsWatchers.put(activity, listener);
        decor.getViewTreeObserver().addOnGlobalLayoutListener(listener);
        decor.post(() -> maybeInjectSettingsRow(activity));
    }

    private void removeSettingsWatcher(Activity activity) {
        ViewTreeObserver.OnGlobalLayoutListener listener = settingsWatchers.remove(activity);
        if (listener == null)
            return;
        View decor = activity.getWindow().getDecorView();
        ViewTreeObserver observer = decor.getViewTreeObserver();
        if (observer.isAlive())
            observer.removeOnGlobalLayoutListener(listener);
    }

    private void maybeInjectSettingsRow(Activity activity) {
        ViewGroup content = activity.findViewById(android.R.id.content);
        if (content == null || content.findViewWithTag(SETTINGS_ROW_TAG) != null)
            return;

        ScrollView scroll = findScrollView(content);
        if (scroll == null || scroll.getChildCount() != 1
                || !(scroll.getChildAt(0) instanceof LinearLayout))
            return;

        LinearLayout root = (LinearLayout) scroll.getChildAt(0);
        if (root.getChildCount() < 2 || !(root.getChildAt(0) instanceof TextView))
            return;

        CharSequence expected = activity.getString(R.string.settings_title);
        CharSequence actual = ((TextView) root.getChildAt(0)).getText();
        if (actual == null || !actual.toString().contentEquals(expected))
            return;

        LinearLayout block = buildSettingsRow(activity);
        block.setTag(SETTINGS_ROW_TAG);

        int index = Math.max(1, root.getChildCount() - 1);
        root.addView(block, index);
    }

    private LinearLayout buildSettingsRow(Activity activity) {
        int primary = themeColor(activity, android.R.attr.textColorPrimary, Color.BLACK);
        int secondary = themeColor(activity, android.R.attr.textColorSecondary, Color.GRAY);
        int control = themeColor(activity, android.R.attr.colorControlNormal, secondary);
        int dividerColor = (control & 0x00FFFFFF) | 0x33000000;

        LinearLayout block = new LinearLayout(activity);
        block.setOrientation(LinearLayout.VERTICAL);

        LinearLayout row = new LinearLayout(activity);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(activity, 16), 0, dp(activity, 16));
        row.setClickable(true);

        TypedValue value = new TypedValue();
        if (activity.getTheme().resolveAttribute(
                android.R.attr.selectableItemBackground, value, true)) {
            row.setBackgroundResource(value.resourceId);
        }

        LinearLayout texts = new LinearLayout(activity);
        texts.setOrientation(LinearLayout.VERTICAL);
        texts.setLayoutParams(new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        TextView title = new TextView(activity);
        title.setText(R.string.gamepad_settings_title);
        title.setTextSize(18);
        title.setTextColor(primary);
        texts.addView(title);

        TextView subtitle = new TextView(activity);
        subtitle.setText(R.string.gamepad_settings_subtitle);
        subtitle.setTextSize(13);
        subtitle.setTextColor(secondary);
        subtitle.setPadding(0, dp(activity, 2), 0, 0);
        texts.addView(subtitle);

        row.addView(texts);

        TextView chevron = new TextView(activity);
        chevron.setText("›");
        chevron.setTextSize(22);
        chevron.setTextColor(secondary);
        row.addView(chevron);

        row.setOnClickListener(v ->
                activity.startActivity(new Intent(activity, GamepadSettingsActivity.class)));

        block.addView(row);

        View divider = new View(activity);
        divider.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, Math.max(1, dp(activity, 1))));
        divider.setBackgroundColor(dividerColor);
        block.addView(divider);
        return block;
    }

    private ScrollView findScrollView(View view) {
        if (view instanceof ScrollView)
            return (ScrollView) view;
        if (!(view instanceof ViewGroup))
            return null;
        ViewGroup group = (ViewGroup) view;
        for (int i = 0; i < group.getChildCount(); i++) {
            ScrollView found = findScrollView(group.getChildAt(i));
            if (found != null)
                return found;
        }
        return null;
    }

    private int themeColor(Activity activity, int attribute, int fallback) {
        android.content.res.TypedArray values =
                activity.obtainStyledAttributes(new int[]{attribute});
        int color = values.getColor(0, fallback);
        values.recycle();
        return color;
    }

    private int dp(Activity activity, int value) {
        return Math.round(value * activity.getResources().getDisplayMetrics().density);
    }
}
