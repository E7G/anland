package com.anland.consumer;

import android.app.Activity;
import android.content.SharedPreferences;
import android.graphics.Insets;
import android.os.Bundle;
import android.graphics.Typeface;
import android.view.Gravity;
import android.view.WindowInsets;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;

/** Settings page for the optional root-backed virtual controller. */
public final class GamepadSettingsActivity extends Activity {
    private static final String PREFS_NAME = VirtualGamepadView.PREFS_NAME;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(24);
        root.setPadding(pad, pad, pad, pad);
        scroll.addView(root);

        TextView title = new TextView(this);
        title.setText(R.string.gamepad_settings_title);
        title.setTextSize(24);
        title.setTypeface(null, Typeface.BOLD);
        title.setPadding(0, 0, 0, dp(12));
        root.addView(title);

        TextView desc = new TextView(this);
        desc.setText(R.string.gamepad_settings_description);
        desc.setTextSize(14);
        desc.setAlpha(0.75f);
        desc.setPadding(0, 0, 0, dp(22));
        root.addView(desc);

        Switch enabled = new Switch(this);
        enabled.setText(R.string.gamepad_enable);
        enabled.setTextSize(18);
        enabled.setChecked(prefs.getBoolean(VirtualGamepadView.KEY_ENABLED, false));
        enabled.setPadding(0, dp(10), 0, dp(10));
        root.addView(enabled);

        TextView opacityLabel = new TextView(this);
        opacityLabel.setText(R.string.gamepad_opacity);
        opacityLabel.setTextSize(16);
        opacityLabel.setPadding(0, dp(22), 0, dp(4));
        root.addView(opacityLabel);

        SeekBar opacity = new SeekBar(this);
        opacity.setMax(100);
        opacity.setMin(20);
        opacity.setProgress(prefs.getInt(VirtualGamepadView.KEY_OPACITY, 46));
        root.addView(opacity);

        TextView opacityValue = new TextView(this);
        opacityValue.setGravity(Gravity.END);
        opacityValue.setAlpha(0.7f);
        opacityValue.setText(opacity.getProgress() + "%");
        root.addView(opacityValue);

        TextView layout = new TextView(this);
        layout.setText(R.string.gamepad_layout_summary);
        layout.setTextSize(14);
        layout.setAlpha(0.72f);
        layout.setPadding(0, dp(22), 0, 0);
        root.addView(layout);

        TextView requirements = new TextView(this);
        requirements.setText(R.string.gamepad_requirements);
        requirements.setTextSize(13);
        requirements.setAlpha(0.62f);
        requirements.setPadding(0, dp(18), 0, 0);
        root.addView(requirements);

        enabled.setOnCheckedChangeListener((buttonView, checked) -> {
            prefs.edit().putBoolean(VirtualGamepadView.KEY_ENABLED, checked).apply();
            if (!checked)
                GamepadBridge.stop();
        });

        opacity.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                opacityValue.setText(progress + "%");
                if (fromUser)
                    prefs.edit().putInt(VirtualGamepadView.KEY_OPACITY, progress).apply();
            }

            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });

        setContentView(scroll);
        getWindow().setDecorFitsSystemWindows(false);
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            Insets in = insets.getInsets(WindowInsets.Type.systemBars());
            v.setPadding(pad + in.left, pad + in.top, pad + in.right, pad + in.bottom);
            return insets;
        });
        root.post(root::requestApplyInsets);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
