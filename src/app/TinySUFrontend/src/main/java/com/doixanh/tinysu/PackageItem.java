package com.doixanh.tinysu;

import android.graphics.drawable.Drawable;

/**
 * Created by dx on 9/2/17.
 */

public class PackageItem {
    private int uid;
    private String name;
    private String packageName;
    private Drawable icon;
    private boolean selected = false;

    public PackageItem(int uid, String name, String packageName, Drawable icon) {
        this.uid = uid;
        this.name = name;
        this.packageName = packageName;
        this.icon = icon;
    }

    public int getUid() {
        return uid;
    }

    public void setUid(int uid) {
        this.uid = uid;
    }

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public Drawable getIcon() {
        return icon;
    }

    public void setIcon(Drawable icon) {
        this.icon = icon;
    }

    public String getPackageName() {
        return packageName;
    }

    public void setPackageName(String packageName) {
        this.packageName = packageName;
    }

    public boolean isSelected() {
        return selected;
    }

    public void setSelected(boolean selected) {
        this.selected = selected;
    }
}
