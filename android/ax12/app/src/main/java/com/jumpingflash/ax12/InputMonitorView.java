package com.jumpingflash.ax12;

import android.content.Context;
import android.graphics.*;
import android.view.View;
import java.util.Locale;

/** Logical raw radio sources. No calibration, commands or robot state is changed here. */
public final class InputMonitorView extends View {
  private final Paint p = new Paint(3);
  private static final Typeface LABEL = Typeface.create("sans-serif-medium", Typeface.NORMAL);
  private short[] values;
  private boolean fresh = false, hadReport = false;
  private long age = 0, number = 0;
  private static final int INK = Color.rgb(25, 45, 91),
      MUTED = Color.rgb(105, 125, 148),
      GREEN = Color.rgb(20, 145, 122),
      RED = Color.rgb(190, 54, 72),
      SKY = Color.rgb(63, 175, 236),
      PALE = Color.rgb(228, 239, 249);

  public InputMonitorView(Context context) {
    super(context);
  }

  public void update(NativeRadioInput.Sample sample, long now) {
    hadReport = sample != null;
    fresh = sample != null && sample.fresh(now);
    age = sample == null ? 0 : Math.max(0, now - sample.at);
    number = sample == null ? 0 : sample.number;
    values = fresh ? sample.values : null;
    setContentDescription(
        fresh
            ? "LIVE radio input monitor, report " + number + ", age " + age + " milliseconds"
            : hadReport ? "STALE radio inputs; controls are not fresh" : "No valid radio reports");
    invalidate();
  }

  private void color(int c) {
    p.setColor(c);
    p.setStyle(Paint.Style.FILL);
    p.setShader(null);
  }

  private void text(Canvas c, String s, float x, float y, int size, int ink) {
    color(ink);
    p.setTextSize(size);
    p.setTypeface(LABEL);
    c.drawText(s, x, y, p);
  }

  private boolean known(int source) {
    return fresh
        && values != null
        && values[source] != Short.MIN_VALUE
        && Math.abs(values[source]) <= 550;
  }

  private float norm(int source) {
    return known(source) ? Math.max(-1, Math.min(1, values[source] / 500f)) : 0;
  }

  private void stick(Canvas c, float x, int sourceX, int sourceY, String label) {
    text(c, label, x, 72, 18, INK);
    color(PALE);
    c.drawRoundRect(x, 86, x + 150, 236, 14, 14, p);
    color(Color.rgb(172, 195, 216));
    p.setStrokeWidth(1);
    c.drawLine(x + 75, 95, x + 75, 227, p);
    c.drawLine(x + 9, 161, x + 141, 161, p);
    if (known(sourceX) && known(sourceY)) {
      color(SKY);
      c.drawCircle(x + 75 + norm(sourceX) * 61, 161 - norm(sourceY) * 61, 12, p);
      text(
          c,
          String.format(Locale.US, "X %+d  Y %+d", values[sourceX], values[sourceY]),
          x,
          259,
          14,
          INK);
    } else text(c, "NO LIVE DATA", x + 13, 166, 15, MUTED);
  }

  private void bar(Canvas c, int index, String name, float y) {
    text(c, name, 365, y + 16, 16, INK);
    float x = 540, w = 500;
    color(PALE);
    c.drawRoundRect(x, y, x + w, y + 22, 6, 6, p);
    color(Color.rgb(172, 195, 216));
    c.drawRect(x + w / 2 - 1, y - 3, x + w / 2 + 1, y + 25, p);
    if (known(index)) {
      float end = x + w / 2 + norm(index) * w / 2;
      color(SKY);
      c.drawRoundRect(Math.min(x + w / 2, end), y, Math.max(x + w / 2, end), y + 22, 5, 5, p);
      text(c, String.format(Locale.US, "%+d", values[index]), 1053, y + 17, 15, INK);
    } else text(c, "—", 1053, y + 17, 15, MUTED);
  }

  private void selector(Canvas c, float x, int source, String name, boolean two) {
    text(c, name, x, 310, 18, INK);
    int n = two ? 2 : 3;
    int selected =
        known(source)
            ? (two
                ? (values[source] < 0 ? 0 : 1)
                : (values[source] < -165 ? 0 : values[source] > 165 ? 2 : 1))
            : -1;
    String[] label = two ? new String[] {"LOW", "HIGH"} : new String[] {"LOW", "MID", "HIGH"};
    for (int i = 0; i < n; i++) {
      float at = x + i * 88;
      color(i == selected ? GREEN : PALE);
      c.drawRoundRect(at, 322, at + 80, 356, 8, 8, p);
      text(c, label[i], at + 12, 345, 14, i == selected ? Color.WHITE : MUTED);
    }
    text(
        c,
        known(source) ? String.format(Locale.US, "raw %+d", values[source]) : "no live data",
        x,
        378,
        14,
        MUTED);
  }

  @Override
  protected void onDraw(Canvas c) {
    super.onDraw(c);
    c.save();
    c.scale(getWidth() / 1120f, getHeight() / 400f);
    text(
        c,
        fresh
            ? "LIVE  •  report " + number + "  •  " + age + " ms ago"
            : hadReport
                ? "STALE  •  last report " + age + " ms ago"
                : "WAITING FOR VALID RADIO REPORTS",
        16,
        29,
        20,
        fresh ? GREEN : RED);
    stick(c, 16, 0, 2, "RUD / THR");
    stick(c, 190, 3, 1, "AIL / ELE");
    int[] sources = {3, 1, 2, 0, 12, 13};
    String[] names = {
      "CH1  Aileron", "CH2  Elevator", "CH3  Throttle", "CH4  Rudder", "S1  Dial", "S2  Dial"
    };
    for (int i = 0; i < sources.length; i++) bar(c, sources[i], names[i], 64 + i * 35);
    selector(c, 16, 21, "SB", false);
    selector(c, 385, 22, "SC", false);
    selector(c, 755, 23, "SD", true);
    c.restore();
  }
}
