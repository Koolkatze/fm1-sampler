package com.ferluht.fm1sampler;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;

/** Replays the draw ops built by the C++ PhoneUI (engine/ui/PhoneUI.cpp) and forwards touches to it. All layout lives in C++. */
public class SamplerView extends View {
    final long h;
    private boolean pictureInPicture;
    float d = 1;                         // layout unit -> px: the UI is laid out for a fixed height of 400 units, so it scales to any phone
    final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    final RectF r = new RectF();
    final Path path = new Path();
    final Typeface regular = Typeface.create(Typeface.MONOSPACE, Typeface.NORMAL), bold = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD);
    final float topDp, bottomDp;

    public SamplerView(Context c, long engine, float topDp, float bottomDp) {
        super(c);
        h = engine; this.topDp = topDp; this.bottomDp = bottomDp;
        setBackgroundColor(0xff14141a);
        p.setAntiAlias(false);
    }

    void setPictureInPicture(boolean pip) { pictureInPicture = pip; invalidate(); }

    static float f(int bits) { return Float.intBitsToFloat(bits); }

    @Override protected void onDraw(Canvas c) {
        long now = SystemClock.uptimeMillis();
        d = getHeight() / 400f;
        Object[] fr = Native.uiFrame(h, getWidth() / d, getHeight() / d, topDp, bottomDp, now, pictureInPicture);
        int[] a = (int[]) fr[0];
        String[] texts = ((String) fr[1]).split("\u0001", -1);
        int saves = 0;
        for (int i = 0; i + 8 < a.length; i += 9) {
            final int type = a[i];
            final float f0 = f(a[i + 1]) * d, f1 = f(a[i + 2]) * d, f2 = f(a[i + 3]) * d, f3 = f(a[i + 4]) * d, f4 = f(a[i + 5]) * d, f5 = f(a[i + 6]) * d;
            final int color = a[i + 7];
            p.setColor(color);
            switch (type) {
            case 0:   // RRECT x y w h r
                p.setStyle(Paint.Style.FILL); r.set(f0, f1, f0 + f2, f1 + f3);
                if (f4 > 0) c.drawRoundRect(r, f4, f4, p); else c.drawRect(r, p);
                break;
            case 1:   // STROKE x y w h r width(dp)
                p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(f5); r.set(f0, f1, f0 + f2, f1 + f3);
                c.drawRoundRect(r, f4, f4, p); p.setStyle(Paint.Style.FILL);
                break;
            case 2: { // TEXT x baseline size align bold   (size is dp: f2 already scaled)
                p.setStyle(Paint.Style.FILL); p.setTextSize(f2);
                p.setTypeface(f(a[i + 5]) != 0 ? bold : regular);
                int al = (int) f(a[i + 4]);
                p.setTextAlign(al == 1 ? Paint.Align.CENTER : al == 2 ? Paint.Align.RIGHT : Paint.Align.LEFT);
                int ti = a[i + 8];
                if (ti >= 0 && ti < texts.length) c.drawText(texts[ti], f0, f1, p);
                break;
            }
            case 3:   // LINE
                p.setStrokeWidth(f4); p.setStrokeCap(Paint.Cap.ROUND); c.drawLine(f0, f1, f2, f3, p);
                break;
            case 4:   // CIRCLE
                p.setStyle(Paint.Style.FILL); c.drawCircle(f0, f1, f2, p);
                break;
            case 5:   // TRI
                p.setStyle(Paint.Style.FILL); path.reset(); path.moveTo(f0, f1); path.lineTo(f2, f3); path.lineTo(f4, f5); path.close(); c.drawPath(path, p);
                break;
            case 6:   // CLIP
                c.save(); saves++; c.clipRect(f0, f1, f0 + f2, f1 + f3);
                break;
            case 7:
                if (saves > 0) { c.restore(); saves--; }
                break;
            }
        }
        while (saves-- > 0) c.restore();
        postInvalidateOnAnimation();
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        if (pictureInPicture) return true;
        int act = e.getActionMasked();
        int a = act == MotionEvent.ACTION_DOWN ? 0 : act == MotionEvent.ACTION_MOVE ? 1 : (act == MotionEvent.ACTION_UP || act == MotionEvent.ACTION_CANCEL) ? 2 : -1;
        if (a < 0) return super.onTouchEvent(e);
        Native.uiTouch(h, a, e.getX() / d, e.getY() / d, SystemClock.uptimeMillis());
        return true;
    }
}
