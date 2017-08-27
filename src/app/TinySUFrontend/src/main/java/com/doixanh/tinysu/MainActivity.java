package com.doixanh.tinysu;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;

import eu.chainfire.libsuperuser.Debug;
import eu.chainfire.libsuperuser.Shell;

public class MainActivity extends Activity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        Debug.setDebug(true);
        new Thread(new Runnable() {
            @Override
            public void run() {
                Log.i("TinySUChecker", "SU availability " + Shell.SU.available());
            }
        }).start();

    }
}
