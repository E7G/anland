package com.anland.consumer;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.util.SparseArray;
import android.view.MotionEvent;
import android.view.View;

/**
 * Lightweight Xbox-style on-screen controller.
 *
 * It draws only when state changes and sends one compact controller snapshot when
 * the state changes. No animation timer or polling loop is used.
 */
public final class VirtualGamepadView extends View {
    public static final String PREFS_NAME = "anland_settings";
    public static final String KEY_ENABLED = "virtual_gamepad_enabled";
    public static final String KEY_OPACITY = "virtual_gamepad_opacity";

    private static final int BTN_A      = 1 << 0;
    private static final int BTN_B      = 1 << 1;
    private static final int BTN_X      = 1 << 2;
    private static final int BTN_Y      = 1 << 3;
    private static final int BTN_LB     = 1 << 4;
    private static final int BTN_RB     = 1 << 5;
    private static final int BTN_BACK   = 1 << 6;
    private static final int BTN_START  = 1 << 7;
    private static final int BTN_GUIDE  = 1 << 8;
    private static final int BTN_L3     = 1 << 9;
    private static final int BTN_R3     = 1 << 10;

    private enum Control {
        LEFT_STICK, RIGHT_STICK, DPAD,
        A, B, X, Y, LB, RB, LT, RT,
        BACK, START, GUIDE, L3, R3
    }

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final SparseArray<Control> pointerControls = new SparseArray<>();

    private float minDim;
    private float stickRadius;
    private float buttonRadius;
    private float smallButtonRadius;

    private float leftX, leftY;
    private float rightX, rightY;
    private float dpadX, dpadY;
    private float faceX, faceY;

    private final RectF lbRect = new RectF();
    private final RectF ltRect = new RectF();
    private final RectF rbRect = new RectF();
    private final RectF rtRect = new RectF();

    private float backX, backY;
    private float startX, startY;
    private float guideX, guideY;
    private float l3X, l3Y;
    private float r3X, r3Y;

    private int buttons;
    private int lx, ly, rx, ry;
    private int lt, rt;
    private int hatX, hatY;
    private int opacityPercent = 46;

    public VirtualGamepadView(Context context) {
        super(context);
        init();
    }

    public VirtualGamepadView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    private void init() {
        setFocusable(false);
        setClickable(false);
        SharedPreferences prefs = getContext().getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        opacityPercent = clamp(prefs.getInt(KEY_OPACITY, 46), 20, 90);

        fill.setStyle(Paint.Style.FILL);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(dp(2));
        text.setTextAlign(Paint.Align.CENTER);
        text.setFakeBoldText(true);
        setWillNotDraw(false);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        minDim = Math.max(1f, Math.min(w, h));
        stickRadius = minDim * 0.095f;
        buttonRadius = minDim * 0.040f;
        smallButtonRadius = minDim * 0.030f;

        leftX = w * 0.155f;
        leftY = h * 0.715f;

        dpadX = w * 0.335f;
        dpadY = h * 0.765f;

        rightX = w * 0.655f;
        rightY = h * 0.785f;

        faceX = w * 0.855f;
        faceY = h * 0.685f;

        float shoulderTop = h * 0.075f;
        float shoulderH = Math.max(dp(34), h * 0.075f);
        float shoulderW = Math.max(dp(78), w * 0.125f);
        float leftShoulderX = w * 0.055f;
        float rightShoulderX = w * 0.945f - shoulderW;

        ltRect.set(leftShoulderX, shoulderTop,
                leftShoulderX + shoulderW, shoulderTop + shoulderH);
        lbRect.set(leftShoulderX, shoulderTop + shoulderH + dp(8),
                leftShoulderX + shoulderW, shoulderTop + shoulderH * 2f + dp(8));
        rtRect.set(rightShoulderX, shoulderTop,
                rightShoulderX + shoulderW, shoulderTop + shoulderH);
        rbRect.set(rightShoulderX, shoulderTop + shoulderH + dp(8),
                rightShoulderX + shoulderW, shoulderTop + shoulderH * 2f + dp(8));

        backX = w * 0.455f;
        startX = w * 0.545f;
        backY = startY = h * 0.695f;
        guideX = w * 0.5f;
        guideY = h * 0.595f;
        l3X = w * 0.435f;
        r3X = w * 0.565f;
        l3Y = r3Y = h * 0.875f;
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        int fillAlpha = 255 * opacityPercent / 100;
        int strokeAlpha = Math.min(255, fillAlpha + 70);
        fill.setColor((fillAlpha << 24) | 0x00151A20);
        stroke.setColor((strokeAlpha << 24) | 0x00FFFFFF);
        text.setColor((strokeAlpha << 24) | 0x00FFFFFF);
        text.setTextSize(Math.max(dp(11), minDim * 0.022f));

        drawStick(canvas, leftX, leftY, stickRadius, lx, ly, "L");
        drawStick(canvas, rightX, rightY, stickRadius, rx, ry, "R");
        drawDpad(canvas);

        float spread = minDim * 0.067f;
        drawRoundButton(canvas, faceX, faceY + spread, buttonRadius, "A",
                (buttons & BTN_A) != 0);
        drawRoundButton(canvas, faceX + spread, faceY, buttonRadius, "B",
                (buttons & BTN_B) != 0);
        drawRoundButton(canvas, faceX - spread, faceY, buttonRadius, "X",
                (buttons & BTN_X) != 0);
        drawRoundButton(canvas, faceX, faceY - spread, buttonRadius, "Y",
                (buttons & BTN_Y) != 0);

        drawRectButton(canvas, ltRect, "LT", lt > 0);
        drawRectButton(canvas, lbRect, "LB", (buttons & BTN_LB) != 0);
        drawRectButton(canvas, rtRect, "RT", rt > 0);
        drawRectButton(canvas, rbRect, "RB", (buttons & BTN_RB) != 0);

        drawRoundButton(canvas, backX, backY, smallButtonRadius, "−",
                (buttons & BTN_BACK) != 0);
        drawRoundButton(canvas, startX, startY, smallButtonRadius, "+",
                (buttons & BTN_START) != 0);
        drawRoundButton(canvas, guideX, guideY, smallButtonRadius, "◎",
                (buttons & BTN_GUIDE) != 0);
        drawRoundButton(canvas, l3X, l3Y, smallButtonRadius, "L3",
                (buttons & BTN_L3) != 0);
        drawRoundButton(canvas, r3X, r3Y, smallButtonRadius, "R3",
                (buttons & BTN_R3) != 0);
    }

    private void drawStick(Canvas canvas, float cx, float cy, float radius,
                           int ax, int ay, String label) {
        canvas.drawCircle(cx, cy, radius, fill);
        canvas.drawCircle(cx, cy, radius, stroke);
        float knobR = radius * 0.47f;
        float kx = cx + (ax / 32767f) * radius * 0.52f;
        float ky = cy + (ay / 32767f) * radius * 0.52f;
        canvas.drawCircle(kx, ky, knobR, fill);
        canvas.drawCircle(kx, ky, knobR, stroke);
        drawCenteredText(canvas, label, kx, ky);
    }

    private void drawDpad(Canvas canvas) {
        float r = stickRadius * 0.78f;
        float arm = r * 0.43f;
        RectF vertical = new RectF(dpadX - arm, dpadY - r,
                dpadX + arm, dpadY + r);
        RectF horizontal = new RectF(dpadX - r, dpadY - arm,
                dpadX + r, dpadY + arm);
        canvas.drawRoundRect(vertical, arm * 0.35f, arm * 0.35f, fill);
        canvas.drawRoundRect(horizontal, arm * 0.35f, arm * 0.35f, fill);
        canvas.drawRoundRect(vertical, arm * 0.35f, arm * 0.35f, stroke);
        canvas.drawRoundRect(horizontal, arm * 0.35f, arm * 0.35f, stroke);

        if (hatX != 0 || hatY != 0) {
            float dotX = dpadX + hatX * r * 0.60f;
            float dotY = dpadY + hatY * r * 0.60f;
            canvas.drawCircle(dotX, dotY, arm * 0.40f, stroke);
        }
    }

    private void drawRoundButton(Canvas canvas, float cx, float cy, float r,
                                 String label, boolean pressed) {
        if (pressed) {
            int original = fill.getColor();
            fill.setColor((Math.min(255, ((original >>> 24) & 0xFF) + 65) << 24)
                    | 0x005A6672);
            canvas.drawCircle(cx, cy, r, fill);
            fill.setColor(original);
        } else {
            canvas.drawCircle(cx, cy, r, fill);
        }
        canvas.drawCircle(cx, cy, r, stroke);
        drawCenteredText(canvas, label, cx, cy);
    }

    private void drawRectButton(Canvas canvas, RectF rect, String label, boolean pressed) {
        float radius = dp(10);
        if (pressed) {
            int original = fill.getColor();
            fill.setColor((Math.min(255, ((original >>> 24) & 0xFF) + 65) << 24)
                    | 0x005A6672);
            canvas.drawRoundRect(rect, radius, radius, fill);
            fill.setColor(original);
        } else {
            canvas.drawRoundRect(rect, radius, radius, fill);
        }
        canvas.drawRoundRect(rect, radius, radius, stroke);
        drawCenteredText(canvas, label, rect.centerX(), rect.centerY());
    }

    private void drawCenteredText(Canvas canvas, String label, float x, float y) {
        Paint.FontMetrics fm = text.getFontMetrics();
        float baseline = y - (fm.ascent + fm.descent) / 2f;
        canvas.drawText(label, x, baseline, text);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        final int action = event.getActionMasked();
        final int actionIndex = event.getActionIndex();

        if (action == MotionEvent.ACTION_DOWN) {
            Control control = hitTest(event.getX(actionIndex), event.getY(actionIndex));
            if (control == null)
                return false; // Let normal Anland touch input handle the gesture.
            pointerControls.put(event.getPointerId(actionIndex), control);
            updateControl(control, event.getX(actionIndex), event.getY(actionIndex), true);
            return true;
        }

        if (action == MotionEvent.ACTION_POINTER_DOWN) {
            Control control = hitTest(event.getX(actionIndex), event.getY(actionIndex));
            if (control != null) {
                pointerControls.put(event.getPointerId(actionIndex), control);
                updateControl(control, event.getX(actionIndex), event.getY(actionIndex), true);
            }
            return true;
        }

        if (action == MotionEvent.ACTION_MOVE) {
            for (int i = 0; i < event.getPointerCount(); i++) {
                Control control = pointerControls.get(event.getPointerId(i));
                if (control != null)
                    updateControl(control, event.getX(i), event.getY(i), true);
            }
            return true;
        }

        if (action == MotionEvent.ACTION_POINTER_UP || action == MotionEvent.ACTION_UP) {
            int id = event.getPointerId(actionIndex);
            Control control = pointerControls.get(id);
            if (control != null) {
                updateControl(control, event.getX(actionIndex), event.getY(actionIndex), false);
                pointerControls.remove(id);
            }
            if (action == MotionEvent.ACTION_UP && pointerControls.size() != 0) {
                pointerControls.clear();
                neutralizeLocal();
            }
            return true;
        }

        if (action == MotionEvent.ACTION_CANCEL) {
            pointerControls.clear();
            neutralizeLocal();
            return true;
        }

        return pointerControls.size() > 0;
    }

    private Control hitTest(float x, float y) {
        if (ltRect.contains(x, y)) return Control.LT;
        if (lbRect.contains(x, y)) return Control.LB;
        if (rtRect.contains(x, y)) return Control.RT;
        if (rbRect.contains(x, y)) return Control.RB;

        float spread = minDim * 0.067f;
        if (inside(x, y, faceX, faceY + spread, buttonRadius * 1.25f)) return Control.A;
        if (inside(x, y, faceX + spread, faceY, buttonRadius * 1.25f)) return Control.B;
        if (inside(x, y, faceX - spread, faceY, buttonRadius * 1.25f)) return Control.X;
        if (inside(x, y, faceX, faceY - spread, buttonRadius * 1.25f)) return Control.Y;

        if (inside(x, y, backX, backY, smallButtonRadius * 1.40f)) return Control.BACK;
        if (inside(x, y, startX, startY, smallButtonRadius * 1.40f)) return Control.START;
        if (inside(x, y, guideX, guideY, smallButtonRadius * 1.40f)) return Control.GUIDE;
        if (inside(x, y, l3X, l3Y, smallButtonRadius * 1.40f)) return Control.L3;
        if (inside(x, y, r3X, r3Y, smallButtonRadius * 1.40f)) return Control.R3;

        if (inside(x, y, leftX, leftY, stickRadius * 1.35f)) return Control.LEFT_STICK;
        if (inside(x, y, rightX, rightY, stickRadius * 1.35f)) return Control.RIGHT_STICK;
        if (inside(x, y, dpadX, dpadY, stickRadius * 1.10f)) return Control.DPAD;
        return null;
    }

    private void updateControl(Control control, float x, float y, boolean pressed) {
        switch (control) {
            case LEFT_STICK:
                if (pressed) {
                    updateLeftStick(x, y);
                } else {
                    lx = ly = 0;
                }
                break;
            case RIGHT_STICK:
                if (pressed) {
                    updateRightStick(x, y);
                } else {
                    rx = ry = 0;
                }
                break;
            case DPAD:
                if (pressed) {
                    float dx = (x - dpadX) / (stickRadius * 0.78f);
                    float dy = (y - dpadY) / (stickRadius * 0.78f);
                    hatX = Math.abs(dx) > 0.24f ? (dx > 0 ? 1 : -1) : 0;
                    hatY = Math.abs(dy) > 0.24f ? (dy > 0 ? 1 : -1) : 0;
                } else {
                    hatX = hatY = 0;
                }
                break;
            case LT: lt = pressed ? 255 : 0; break;
            case RT: rt = pressed ? 255 : 0; break;
            case A: setButton(BTN_A, pressed); break;
            case B: setButton(BTN_B, pressed); break;
            case X: setButton(BTN_X, pressed); break;
            case Y: setButton(BTN_Y, pressed); break;
            case LB: setButton(BTN_LB, pressed); break;
            case RB: setButton(BTN_RB, pressed); break;
            case BACK: setButton(BTN_BACK, pressed); break;
            case START: setButton(BTN_START, pressed); break;
            case GUIDE: setButton(BTN_GUIDE, pressed); break;
            case L3: setButton(BTN_L3, pressed); break;
            case R3: setButton(BTN_R3, pressed); break;
        }
        emitState();
        invalidate();
    }

    private void updateLeftStick(float x, float y) {
        float dx = (x - leftX) / stickRadius;
        float dy = (y - leftY) / stickRadius;
        float mag = (float) Math.hypot(dx, dy);
        if (mag > 1f) {
            dx /= mag;
            dy /= mag;
        }
        lx = Math.round(dx * 32767f);
        ly = Math.round(dy * 32767f);
    }

    private void updateRightStick(float x, float y) {
        float dx = (x - rightX) / stickRadius;
        float dy = (y - rightY) / stickRadius;
        float mag = (float) Math.hypot(dx, dy);
        if (mag > 1f) {
            dx /= mag;
            dy /= mag;
        }
        rx = Math.round(dx * 32767f);
        ry = Math.round(dy * 32767f);
    }

    private void setButton(int bit, boolean pressed) {
        if (pressed) buttons |= bit;
        else buttons &= ~bit;
    }

    private void emitState() {
        GamepadBridge.sendState(buttons, lx, ly, rx, ry, lt, rt, hatX, hatY);
    }

    public void neutralize() {
        pointerControls.clear();
        neutralizeLocal();
    }

    private void neutralizeLocal() {
        buttons = 0;
        lx = ly = rx = ry = 0;
        lt = rt = 0;
        hatX = hatY = 0;
        emitState();
        invalidate();
    }

    @Override
    protected void onDetachedFromWindow() {
        neutralize();
        super.onDetachedFromWindow();
    }

    private boolean inside(float x, float y, float cx, float cy, float r) {
        float dx = x - cx;
        float dy = y - cy;
        return dx * dx + dy * dy <= r * r;
    }

    private float dp(float value) {
        return value * getResources().getDisplayMetrics().density;
    }

    private static int clamp(int v, int min, int max) {
        return Math.max(min, Math.min(max, v));
    }
}
