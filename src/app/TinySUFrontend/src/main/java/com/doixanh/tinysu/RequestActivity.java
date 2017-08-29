package com.doixanh.tinysu;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.drawable.Drawable;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;
import android.view.View;
import android.widget.ImageView;
import android.widget.TextView;

import com.doixanh.tinysu.R;

import java.io.File;
import java.io.IOException;

public class RequestActivity extends Activity {

    private static final String TAG = "TinySUFrontend";
    private static final String YES = "YaY!\0";

    private void write(String path, String result) {
        Log.i(TAG, "Writing response " + result + " to path " + path);
        LocalSocket socket = new LocalSocket(LocalSocket.SOCKET_STREAM);
        LocalSocketAddress endpoint = new LocalSocketAddress(path);
        try {
            socket.connect(endpoint);
            socket.getOutputStream().write(result.getBytes());
            socket.getOutputStream().flush();
            socket.close();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_request);

        // get the param
        Intent intent = getIntent();
        int uid = intent.getIntExtra("uid", 0);
        final String path = intent.getStringExtra("path");
        Log.i(TAG, "Requested permission for uid=" + uid + ", path=" + path);

        Log.i(TAG, "Path: " + path);
        File directory = new File("/su");
        File[] files = directory.listFiles();
        for (int i = 0; i < files.length; i++) {
            Log.i(TAG, "FileName:" + files[i].getName());
        }

        // solve to package and app name
        String names[] = getPackageManager().getPackagesForUid(uid);
        if (names.length > 0) {
            PackageManager packageManager = getApplicationContext().getPackageManager();
            try {
                String appName = (String) packageManager.getApplicationLabel(packageManager.getApplicationInfo(names[0], PackageManager.GET_META_DATA));
                TextView tv = findViewById(R.id.name);
                tv.setText(appName);

                Drawable logo = getPackageManager().getApplicationIcon(names[0]);
                ((ImageView) findViewById(R.id.logo)).setImageDrawable(logo);
            } catch (PackageManager.NameNotFoundException e) {
                e.printStackTrace();
            }

            String all = "";
            for (String name : names) {
                all += name + "\n";
            }
            ((TextView) findViewById(R.id.packages)).setText(all);
        }

        // binding
        findViewById(R.id.no).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                write(path, "Nah.");
                finish();
            }
        });

        findViewById(R.id.yes).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                write(path, YES);
                finish();
            }
        });

    }
}