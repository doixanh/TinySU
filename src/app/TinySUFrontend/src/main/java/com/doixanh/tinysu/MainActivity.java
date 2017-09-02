package com.doixanh.tinysu;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.pm.PackageManager;
import android.graphics.drawable.Drawable;
import android.os.Bundle;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuInflater;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.CheckBox;
import android.widget.ImageView;
import android.widget.ListView;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class MainActivity extends Activity {
    private static final String TAG = "TinySUManager";

    private ArrayList<PackageItem> items;
    private PackageArrayAdapter adapter;
    private ListView listView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        /*Debug.setDebug(true);
        new Thread(new Runnable() {
            @Override
            public void run() {
                Log.i("TinySUChecker", "SU availability " + Shell.SU.available());
            }
        }).start();*/

        // okay, I know RecyclerView should be the way to go, but it's only available in support lib v7
        // And that (along with support lib v4) increases my TinySU.apk to a huge margin.
        // That isn't tiny anymore. Disliked.

        initAttributes();
        initControls();
        initData();
    }

    private void initAttributes() {
        items = new ArrayList<>();
        adapter = new PackageArrayAdapter(this, R.layout.app_item, items);
    }

    private void initControls() {
        listView = (ListView) findViewById(R.id.list);
        listView.setAdapter(adapter);
    }

    private void initData() {
        // should do this in background.
        PackageManager packageManager = getApplicationContext().getPackageManager();
        try {
            File in = new File(getFilesDir(), "trusted.txt");
            BufferedReader br = new BufferedReader(new FileReader(in));
            String line;
            while ((line = br.readLine()) != null) {
                int uid = Integer.valueOf(line);
                if (uid != 0) {
                    String names[] = packageManager.getPackagesForUid(uid);
                    try {
                        String appName = (String) packageManager.getApplicationLabel(packageManager.getApplicationInfo(names[0], PackageManager.GET_META_DATA));
                        Drawable icon = packageManager.getApplicationIcon(names[0]);
                        items.add(new PackageItem(uid, appName, names[0], icon));
                    } catch (PackageManager.NameNotFoundException e) {
                    }
                }
            }
        } catch (FileNotFoundException e) {
            e.printStackTrace();
        } catch (IOException e) {
            e.printStackTrace();
        }
        adapter.notifyDataSetChanged();
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        MenuInflater inflater = getMenuInflater();
        inflater.inflate(R.menu.main, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        switch (item.getItemId()) {
            case R.id.delete:
                delete();
                break;
            case R.id.all:
                selectAll();
                break;
        }
        return true;
    }

    /**
     * Delete selected items from the backend array
     */
    private void delete() {
        DialogInterface.OnClickListener listener = new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                switch (which) {
                    case DialogInterface.BUTTON_POSITIVE:
                        for (int i = items.size() - 1; i >= 0; i--) {
                            if (items.get(i).isSelected()) {
                                items.remove(i);
                            }
                        }
                        saveList();
                        break;
                    case DialogInterface.BUTTON_NEGATIVE:
                        break;
                }
            }
        };
        new AlertDialog.Builder(this)
                .setMessage(R.string.delete_confirm)
                .setPositiveButton(R.string.yep, listener)
                .setNegativeButton(R.string.no2, listener).show();
    }

    /**
     * Save the array back to the trusted file
     */
    private void saveList() {
        try {
            File out = new File(getFilesDir(), "trusted.txt");
            FileWriter writer = new FileWriter(out);
            for (PackageItem item: items) {
                writer.append(String.valueOf(item.getUid()) + "\n");
            }
            writer.flush();
            writer.close();
        } catch (IOException e) {
            Log.e(TAG, "Cannot save trusted uid at " + getFilesDir().getAbsolutePath());
            e.printStackTrace();
        }
        adapter.notifyDataSetChanged();
    }

    /**
     * Select/Deselect all items in the backend array
     */
    private void selectAll() {
        boolean select = false;
        for (PackageItem item: items) {
            if (!item.isSelected()) {
                select = true;
                break;
            }
        }
        for (PackageItem item: items) {
            item.setSelected(select);
        }
        adapter.notifyDataSetChanged();
    }

    private class PackageArrayAdapter extends ArrayAdapter<PackageItem> {
        public PackageArrayAdapter(Context context, int resource, List<PackageItem> objects) {
            super(context, resource, objects);
        }

        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            View view = convertView;
            if (view == null) {
                view = LayoutInflater.from(MainActivity.this).inflate(R.layout.app_item, null);
            }

            PackageItem item = getItem(position);
            ((TextView) view.findViewById(R.id.name)).setText(item.getName());
            ((TextView) view.findViewById(R.id.packages)).setText(item.getPackageName());
            ((ImageView) view.findViewById(R.id.icon)).setImageDrawable(item.getIcon());
            ((CheckBox) view.findViewById(R.id.check)).setChecked(item.isSelected());

            view.setTag(position);

            view.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    CheckBox check = (CheckBox) v.findViewById(R.id.check);
                    check.toggle();
                    int position = (int) v.getTag();
                    getItem(position).setSelected(check.isChecked());
                }
            });

            return view;
        }
    }
}
