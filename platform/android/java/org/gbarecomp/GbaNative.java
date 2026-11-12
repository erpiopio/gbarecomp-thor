package org.gbarecomp;

/**
 * JNI entry points exported by the gbarecomp runtime (libmain.so).
 *
 * The native side lives in src/runtime/host_window.cpp. Every method here is
 * safe to call from the UI thread; the runtime reads the values on its own
 * thread at the next present.
 */
public final class GbaNative {
    private GbaNative() {}

    /** Read-only snapshot of the first character slot, or null before initialization. */
    public static native int[] getStatusSnapshot();

    /** Datos del mapa regional en tiempo real, solo lectura. */
    public static native int[] getMapSnapshot();
    public static native byte[] getScouterGraphics();
    /** Original ROM-backed Scouter, [revision,width,height,ARGB...], null if unchanged. */
    public static native int[] getOriginalScouterMap(int revision);

    /** Solicita pulsaciones de L para cambiar la técnica activa. */
    public static native void requestSkillCycles(int cycles);

    /** Solicita utilizar un objeto del inventario original. */
    public static native void requestUseItem(int itemId);
    public static native int requestEquipmentChange(int character, int category,
            int record, int itemId, int owner, int stateEpoch);

    /**
     * Safe-area insets in physical pixels: display cutouts and mandatory
     * system-gesture regions. Host chrome (touch pad, native buttons) is laid
     * out inside them; the game image may extend underneath.
     */
    public static native void setSafeInsets(int left, int top, int right, int bottom);

    /**
     * Physical pixel density of the panel in the current display mode
     * (DisplayMetrics from getRealMetrics). Touch targets and gesture
     * thresholds are sized in millimetres from it; densityDpi is a user-
     * scalable logical setting and can differ from the glass by 25% or more.
     */
    public static native void setDisplayDpi(float xdpi, float ydpi);
}
