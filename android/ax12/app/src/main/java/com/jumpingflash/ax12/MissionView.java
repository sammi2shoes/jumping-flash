package com.jumpingflash.ax12;

import android.content.Context;
import android.graphics.*;
import android.view.View;

/** Original vector scenery: bright skies, floating islands and a mechanical jump scout. */
public final class MissionView extends View {
  private final Paint p = new Paint(3);
  private static final int INK = Color.rgb(25, 45, 91),
      SKY = Color.rgb(66, 181, 244),
      YELLOW = Color.rgb(255, 212, 62),
      MINT = Color.rgb(87, 224, 169);
  public boolean linked, referenced, armed, fault, stopping, directionTest;
  public float degrees;

  public MissionView(Context c) {
    super(c);
    setContentDescription("Single-leg test rig mission display");
  }

  private void fill(int c) {
    p.setShader(null);
    p.setColor(c);
    p.setStyle(Paint.Style.FILL);
  }

  private void box(Canvas c, float x, float y, float w, float h, int color) {
    fill(color);
    c.drawRoundRect(x, y, x + w, y + h, 12, 12, p);
  }

  private void poly(Canvas c, int color, float... xy) {
    Path path = new Path();
    path.moveTo(xy[0], xy[1]);
    for (int i = 2; i < xy.length; i += 2) path.lineTo(xy[i], xy[i + 1]);
    path.close();
    fill(color);
    c.drawPath(path, p);
  }

  private void label(Canvas c, String s, float x, float y, int size, int color) {
    fill(color);
    p.setTypeface(Typeface.create("sans-serif-black", Typeface.BOLD));
    p.setTextSize(size);
    c.drawText(s, x, y, p);
  }

  private void island(Canvas c, float x, float y, float scale) {
    c.save();
    c.translate(x, y);
    c.scale(scale, scale);
    poly(c, INK, -96, 0, 0, -35, 96, 0, 0, 125);
    poly(c, Color.rgb(52, 113, 167), -96, 0, 0, 28, 0, 125);
    poly(c, MINT, -96, 0, 0, -35, 96, 0, 0, 30);
    poly(c, Color.rgb(155, 245, 181), -96, 0, 0, -35, 0, 30);
    c.restore();
  }

  @Override
  protected void onDraw(Canvas canvas) {
    super.onDraw(canvas);
    Canvas c = canvas;
    c.save();
    c.scale(getWidth() / 640f, getHeight() / 370f);
    p.setShader(
        new LinearGradient(
            0, 0, 0, 370, new int[] {Color.rgb(136, 222, 255), SKY}, null, Shader.TileMode.CLAMP));
    p.setStyle(Paint.Style.FILL);
    c.drawRoundRect(0, 0, 640, 370, 22, 22, p);
    p.setShader(null);
    fill(Color.rgb(255, 237, 130));
    c.drawCircle(535, 76, 38, p);
    box(c, 358, 83, 100, 15, Color.WHITE);
    box(c, 390, 71, 42, 26, Color.WHITE);
    box(c, 61, 109, 109, 14, Color.WHITE);
    box(c, 86, 96, 47, 25, Color.WHITE);
    island(c, 535, 206, .52f);
    island(c, 98, 222, .42f);
    island(c, 331, 284, 1.12f);
    fill(Color.WHITE);
    for (int i = 0; i < 9; i++) {
      float x = 132 + i * 33;
      float y = 207 - (float) Math.sin(i / 8f * Math.PI) * 100;
      c.drawCircle(x, y, 3, p);
    }
    // Chunky mechanical scout with antenna ears; drawn here, no game artwork bundled.
    c.save();
    c.translate(319, 180);
    c.rotate(-8);
    box(c, -46, -111, 24, 66, INK);
    box(c, -42, -108, 16, 57, Color.WHITE);
    box(c, 22, -111, 24, 66, INK);
    box(c, 26, -108, 16, 57, YELLOW);
    box(c, -62, -58, 124, 88, INK);
    box(c, -55, -51, 110, 73, Color.WHITE);
    box(c, -44, -35, 88, 35, INK);
    box(c, -36, -29, 72, 22, Color.rgb(83, 228, 236));
    box(c, -27, -26, 11, 15, Color.WHITE);
    box(c, 15, -26, 11, 15, Color.WHITE);
    box(c, -33, 26, 66, 33, INK);
    box(c, -27, 29, 54, 23, YELLOW);
    poly(c, INK, -24, 55, -8, 55, -21, 94, -39, 94);
    poly(c, INK, 8, 55, 24, 55, 40, 94, 22, 94);
    box(c, -55, 87, 44, 19, Color.rgb(255, 113, 112));
    box(c, 12, 87, 44, 19, Color.rgb(255, 113, 112));
    fill(INK);
    c.drawCircle(-65, -10, 13, p);
    c.drawCircle(65, -10, 13, p);
    c.restore();
    box(c, 18, 18, 178, 34, INK);
    label(c, "MISSION 01 / TEST RIG", 28, 40, 16, Color.WHITE);
    box(c, 18, 312, 604, 40, Color.WHITE);
    label(
        c,
        fault
            ? "ROBOT FAULT"
            : stopping
                ? "STOPPING"
                : armed ? "LEG ARMED" : linked ? "ROBOT LINKED" : "READY TO CONNECT",
        30,
        339,
        19,
        INK);
    label(
        c,
        directionTest
            ? "80° / 2.5s GUARD"
            : referenced
                ? String.format(java.util.Locale.US, "%.1f° / 80°", degrees)
                : "ZERO NOT SET",
        427,
        338,
        18,
        INK);
    c.restore();
  }
}
