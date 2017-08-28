package com.doixanh.tinysu;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.drawable.Drawable;
import android.os.Bundle;
import android.widget.ImageView;
import android.widget.TextView;

import com.doixanh.tinysu.R;

public class RequestActivity extends Activity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_request);

        Intent intent = getIntent();
        int uid = intent.getIntExtra("uid", 0);
        String names[] = getPackageManager().getPackagesForUid(uid);


        PackageManager packageManager= getApplicationContext().getPackageManager();
        try {
            String appName = (String) packageManager.getApplicationLabel(packageManager.getApplicationInfo(names[0], PackageManager.GET_META_DATA));
            TextView tv = findViewById(R.id.name);
            tv.setText(appName);

            Drawable logo = getPackageManager().getApplicationIcon(names[0]);
            ((ImageView)findViewById(R.id.logo)).setImageDrawable(logo);
        } catch (PackageManager.NameNotFoundException e) {
            e.printStackTrace();
        }

        String all = "";
        for (String name: names) {
            all += name + "\n";
        }
        ((TextView) findViewById(R.id.packages)).setText(all);
    }
}