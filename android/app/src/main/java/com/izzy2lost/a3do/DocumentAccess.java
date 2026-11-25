package com.izzy2lost.a3do;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ComponentName;
import android.content.pm.ResolveInfo;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Scoped folder access through the Storage Access Framework.
 *
 * The user picks the folders their games live in and the app gets read access to
 * exactly those, persisted across restarts. Deliberately NOT all-files access:
 * that is a sensitive permission requiring a Play declaration and review, and
 * this app does not need it — it needs the folder the user points at.
 *
 * The consequence, which shapes the native side: SAF hands out content URIs, not
 * paths. Nothing here can be fopen'd. Files are read by asking the system for a
 * descriptor and passing that down, which is why {@code Disc} grew an
 * open-by-descriptor path.
 */
public final class DocumentAccess {

    private static final String TAG = "a3DO";
    private static final String PREFS = "retro3do.storage";
    private static final String KEY_ROOTS = "granted_roots";

    public static final int REQUEST_PICK_FOLDER = 0x3D0F;
    public static final int REQUEST_ADD_GAMES = 0x3D10;

    /**
     * Android TV's stand-ins for DocumentsUI. They resolve the picker intents
     * and then only say "You don't have an app that can do this".
     */
    private static final String STUB_PACKAGE = "com.android.tv.frameworkpackagestubs";

    /** What a game can be, for "Add games" (a cue's .bin is a disc file too). */
    private static final String[] DISC_EXTENSIONS = {
            ".iso", ".bin", ".cue", ".img", ".chd" };

    private final Activity activity;

    public DocumentAccess(Activity activity) {
        this.activity = activity;
    }

    /**
     * Whether the system folder picker is real. Android TV ships a stub in its
     * place (com.android.tv.frameworkpackagestubs), and some boxes have no
     * picker at all; there the library is built with "Add games" instead.
     */
    public boolean folderPickerWorks() {
        ResolveInfo r = activity.getPackageManager().resolveActivity(
                new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), 0);
        return r != null && r.activityInfo != null &&
               !r.activityInfo.packageName.startsWith(STUB_PACKAGE);
    }

    /**
     * a3DO's own Games folder, for devices without a folder picker. Games are
     * copied in here by "Add games", and it can also be filled from a computer
     * (adb push) since it is app-specific external storage.
     */
    public File localGamesDir() {
        File base = activity.getExternalFilesDir(null);
        if (base == null) base = activity.getFilesDir();
        File games = new File(base, "Games");
        if (!games.isDirectory() && !games.mkdirs()) {
            Log.w(TAG, "Could not create " + games);
        }
        return games;
    }

    /** A plain filesystem path (a3DO's own Games folder), not a content URI. */
    public static boolean isLocalPath(String target) {
        return target != null && target.startsWith("/");
    }

    /** True when the local Games folder holds anything that looks like a disc. */
    public boolean hasLocalGames() {
        return hasDisc(localGamesDir(), 0);
    }

    private static boolean hasDisc(File dir, int depth) {
        File[] files = dir.listFiles();
        if (files == null || depth > 8) return false;
        for (File f : files) {
            if (f.getName().startsWith(".")) continue;
            if (f.isDirectory() ? hasDisc(f, depth + 1) : isDiscName(f.getName())) return true;
        }
        return false;
    }

    private static boolean isDiscName(String name) {
        String lower = name.toLowerCase(Locale.ROOT);
        for (String ext : DISC_EXTENSIONS) {
            if (lower.endsWith(ext)) return true;
        }
        return false;
    }

    /**
     * "Add games": several files picked in whatever file manager the device
     * has, to be copied into the local Games folder. GET_CONTENT rather than
     * OPEN_DOCUMENT, because TV file managers (CX File Explorer and the like)
     * answer it, and the stub is left out of the chooser since it would only
     * close again. Returns false when there is no file manager at all.
     */
    public boolean addGames() {
        Intent get = new Intent(Intent.ACTION_GET_CONTENT)
                .addCategory(Intent.CATEGORY_OPENABLE)
                .setType("*/*")
                .putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        List<ResolveInfo> handlers =
                activity.getPackageManager().queryIntentActivities(get, 0);
        List<ResolveInfo> real = new ArrayList<>();
        List<ComponentName> stubs = new ArrayList<>();
        for (ResolveInfo r : handlers) {
            ComponentName c = new ComponentName(r.activityInfo.packageName, r.activityInfo.name);
            if (r.activityInfo.packageName.startsWith(STUB_PACKAGE)) stubs.add(c);
            else real.add(r);
        }
        if (real.isEmpty()) return false;
        Intent launch;
        if (real.size() == 1) {
            launch = get.setClassName(real.get(0).activityInfo.packageName,
                                      real.get(0).activityInfo.name);
        } else {
            launch = Intent.createChooser(get, "Add games");
            if (!stubs.isEmpty()) {
                launch.putExtra(Intent.EXTRA_EXCLUDE_COMPONENTS,
                                stubs.toArray(new ComponentName[0]));
            }
        }
        try {
            activity.startActivityForResult(launch, REQUEST_ADD_GAMES);
            return true;
        } catch (ActivityNotFoundException e) {
            return false;
        }
    }

    /** The picked files of an "Add games" result. */
    public static List<Uri> pickedUris(Intent data) {
        List<Uri> uris = new ArrayList<>();
        if (data == null) return uris;
        android.content.ClipData clip = data.getClipData();
        if (clip != null) {
            for (int i = 0; i < clip.getItemCount(); i++) {
                Uri u = clip.getItemAt(i).getUri();
                if (u != null) uris.add(u);
            }
        } else if (data.getData() != null) {
            uris.add(data.getData());
        }
        return uris;
    }

    /**
     * Copies one picked file into the local Games folder under its own name.
     * Returns false, copying nothing, when it is not a disc file. The same
     * file picked again (same name and size) is not copied twice. Call off the
     * main thread: a disc image is hundreds of megabytes.
     */
    public boolean importGame(Uri uri) throws Exception {
        String name = null;
        long size = -1;
        try (Cursor c = activity.getContentResolver().query(uri, new String[] {
                android.provider.OpenableColumns.DISPLAY_NAME,
                android.provider.OpenableColumns.SIZE }, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                name = c.getString(0);
                if (!c.isNull(1)) size = c.getLong(1);
            }
        }
        if (name == null) name = uri.getLastPathSegment();
        if (name == null) return false;
        name = name.substring(name.lastIndexOf('/') + 1)
                   .replaceAll("[\\\\/:*?\"<>|]", "_").trim();
        if (name.isEmpty() || name.startsWith(".") || !isDiscName(name)) return false;

        File dir = localGamesDir();
        File target = new File(dir, name);
        if (target.isFile() && size >= 0 && target.length() == size) return true;
        File part = new File(dir, "." + name + ".part");
        try (InputStream in = activity.getContentResolver().openInputStream(uri);
             FileOutputStream out = new FileOutputStream(part)) {
            if (in == null) throw new Exception("Cannot read " + name);
            byte[] buffer = new byte[1 << 20];
            int read;
            while ((read = in.read(buffer)) > 0) out.write(buffer, 0, read);
            out.getFD().sync();
        } catch (Exception e) {
            part.delete();
            throw e;
        }
        // A cue's tracks keep their names, so the cue still finds them.
        if (target.exists()) target.delete();
        if (!part.renameTo(target)) {
            part.delete();
            throw new Exception("Cannot save " + name);
        }
        return true;
    }

    /** Show the system folder picker. The result arrives in onActivityResult. */
    public void pickFolder() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        activity.startActivityForResult(intent, REQUEST_PICK_FOLDER);
    }

    /**
     * Record a granted folder. The permission must be taken persistably here and
     * now: without takePersistableUriPermission the grant dies with the process,
     * and the app silently forgets the user's library on every restart — which
     * looks like a bug in the app rather than a missing call.
     */
    public boolean onFolderGranted(Uri treeUri) {
        if (treeUri == null) {
            return false;
        }
        try {
            activity.getContentResolver().takePersistableUriPermission(
                    treeUri, Intent.FLAG_GRANT_READ_URI_PERMISSION |
                             Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        } catch (SecurityException e) {
            Log.w(TAG, "Could not persist access to " + treeUri, e);
            return false;
        }

        SharedPreferences prefs =
                activity.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        String stored = prefs.getString(KEY_ROOTS, "");
        String encoded = treeUri.toString();
        if (!stored.contains(encoded)) {
            stored = stored.isEmpty() ? encoded : stored + "\n" + encoded;
            prefs.edit().putString(KEY_ROOTS, stored).apply();
        }
        return true;
    }

    /** The folders the user has granted, as tree URI strings. */
    public String[] grantedRoots() {
        SharedPreferences prefs =
                activity.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        String stored = prefs.getString(KEY_ROOTS, "");
        if (!folderPickerWorks()) {
            // Nothing can be granted here; a3DO's own Games folder is the root.
            return new String[] { localGamesDir().getAbsolutePath() };
        }
        if (stored.isEmpty()) {
            return new String[0];
        }

        // A grant can be revoked from system settings, or the folder removed.
        // Only report the ones still actually held, or the browser offers
        // entries that fail the moment they are opened.
        List<String> live = new ArrayList<>();
        for (String candidate : stored.split("\n")) {
            if (candidate.isEmpty()) continue;
            for (android.content.UriPermission held :
                    activity.getContentResolver().getPersistedUriPermissions()) {
                if (held.getUri().toString().equals(candidate) && held.isReadPermission()) {
                    live.add(candidate);
                    break;
                }
            }
        }
        return live.toArray(new String[0]);
    }

    public void forgetRoot(String treeUri) {
        SharedPreferences prefs =
                activity.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        StringBuilder kept = new StringBuilder();
        for (String candidate : prefs.getString(KEY_ROOTS, "").split("\n")) {
            if (candidate.isEmpty() || candidate.equals(treeUri)) continue;
            if (kept.length() > 0) kept.append('\n');
            kept.append(candidate);
        }
        prefs.edit().putString(KEY_ROOTS, kept.toString()).apply();

        try {
            activity.getContentResolver().releasePersistableUriPermission(
                    Uri.parse(treeUri), Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception e) {
            Log.w(TAG, "Could not release " + treeUri, e);
        }
    }

    /** A human-readable name for a granted folder, for the browser's shortcuts. */
    public String displayNameOf(String uri) {
        if (isLocalPath(uri)) {
            return new File(uri).equals(localGamesDir()) ? "a3DO Games" : new File(uri).getName();
        }
        try {
            Uri tree = Uri.parse(uri);
            String documentId = DocumentsContract.getTreeDocumentId(tree);
            // Document ids look like "primary:Games/3DO"; the part after the
            // colon is what a person recognises.
            int colon = documentId.indexOf(':');
            String readable = colon >= 0 ? documentId.substring(colon + 1) : documentId;
            if (readable.isEmpty()) {
                return "Storage";
            }
            return readable;
        } catch (Exception e) {
            return "Folder";
        }
    }

    /**
     * List one folder. Each entry is "D|name|documentUri" or "F|name|documentUri",
     * newline separated — flat text because it crosses to C++ and a string is
     * far less to get wrong than a structured JNI return.
     *
     * @param uri a tree URI (a granted root) or a document URI of a subfolder.
     */
    public String listFolder(String uri) {
        StringBuilder out = new StringBuilder();
        if (isLocalPath(uri)) {
            File[] files = new File(uri).listFiles();
            if (files == null) return "";
            List<File> sorted = new ArrayList<>();
            for (File f : files) {
                if (!f.getName().startsWith(".")) sorted.add(f);
            }
            Collections.sort(sorted, new Comparator<File>() {
                @Override
                public int compare(File a, File b) {
                    if (a.isDirectory() != b.isDirectory()) return a.isDirectory() ? -1 : 1;
                    return a.getName().compareToIgnoreCase(b.getName());
                }
            });
            for (File f : sorted) {
                out.append(f.isDirectory() ? "D" : "F").append('|').append(f.getName())
                   .append('|').append(f.getAbsolutePath()).append('\n');
            }
            return out.toString();
        }
        try {
            Uri parsed = Uri.parse(uri);

            // A tree URI and a document URI need different starting points, and
            // using the wrong one returns an empty list rather than an error.
            String documentId;
            if (DocumentsContract.isDocumentUri(activity, parsed)) {
                documentId = DocumentsContract.getDocumentId(parsed);
            } else {
                documentId = DocumentsContract.getTreeDocumentId(parsed);
            }

            Uri children =
                    DocumentsContract.buildChildDocumentsUriUsingTree(parsed, documentId);

            List<String[]> rows = new ArrayList<>();
            try (Cursor cursor = activity.getContentResolver().query(children,
                    new String[] {
                            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                            DocumentsContract.Document.COLUMN_MIME_TYPE,
                    }, null, null, null)) {
                if (cursor == null) {
                    return "";
                }
                while (cursor.moveToNext()) {
                    String childId = cursor.getString(0);
                    String name = cursor.getString(1);
                    String mime = cursor.getString(2);
                    boolean isDirectory =
                            DocumentsContract.Document.MIME_TYPE_DIR.equals(mime);
                    Uri childUri =
                            DocumentsContract.buildDocumentUriUsingTree(parsed, childId);
                    rows.add(new String[] {
                            isDirectory ? "D" : "F", name, childUri.toString() });
                }
            }

            // Folders first, then files, each alphabetically.
            Collections.sort(rows, new Comparator<String[]>() {
                @Override
                public int compare(String[] a, String[] b) {
                    if (!a[0].equals(b[0])) return a[0].equals("D") ? -1 : 1;
                    return a[1].compareToIgnoreCase(b[1]);
                }
            });

            for (String[] row : rows) {
                out.append(row[0]).append('|').append(row[1]).append('|')
                   .append(row[2]).append('\n');
            }
        } catch (Exception e) {
            Log.w(TAG, "Could not list " + uri, e);
        }
        return out.toString();
    }

    /**
     * Open a document for reading and hand back a raw descriptor.
     *
     * The descriptor is detached, so the native side owns it and must close it.
     * Returning the ParcelFileDescriptor's fd without detaching would let Java
     * close it underneath C++ at the next garbage collection — an intermittent
     * read failure that would be extremely hard to attribute.
     */
    public int openDocument(String documentUri) {
        if (isLocalPath(documentUri)) {
            try {
                return ParcelFileDescriptor.open(new File(documentUri),
                        ParcelFileDescriptor.MODE_READ_ONLY).detachFd();
            } catch (Exception e) {
                Log.w(TAG, "Could not open " + documentUri, e);
                return -1;
            }
        }
        try {
            ParcelFileDescriptor pfd = activity.getContentResolver()
                    .openFileDescriptor(Uri.parse(documentUri), "r");
            if (pfd == null) {
                return -1;
            }
            return pfd.detachFd();
        } catch (Exception e) {
            Log.w(TAG, "Could not open " + documentUri, e);
            return -1;
        }
    }

    /** True only when the chosen tree still has a persisted write grant. */
    public boolean canWrite(String treeUri) {
        if (treeUri == null || treeUri.isEmpty()) return false;
        for (android.content.UriPermission held :
                activity.getContentResolver().getPersistedUriPermissions()) {
            if (held.getUri().toString().equals(treeUri) &&
                held.isReadPermission() && held.isWritePermission()) return true;
        }
        return false;
    }

    private Uri folderDocumentUri(String uri) {
        Uri parsed = Uri.parse(uri);
        if (DocumentsContract.isDocumentUri(activity, parsed)) return parsed;
        return DocumentsContract.buildDocumentUriUsingTree(
                parsed, DocumentsContract.getTreeDocumentId(parsed));
    }

    private Uri findChild(Uri parent, String wantedName) throws Exception {
        String parentId = DocumentsContract.getDocumentId(parent);
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(parent, parentId);
        try (Cursor cursor = activity.getContentResolver().query(children,
                new String[] { DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                               DocumentsContract.Document.COLUMN_DISPLAY_NAME },
                null, null, null)) {
            if (cursor == null) return null;
            while (cursor.moveToNext()) {
                if (wantedName.equalsIgnoreCase(cursor.getString(1))) {
                    return DocumentsContract.buildDocumentUriUsingTree(
                            parent, cursor.getString(0));
                }
            }
        }
        return null;
    }

    /** Create (or reuse) one directory below a granted tree/document URI. */
    public String ensureDirectory(String parentUri, String name) throws Exception {
        Uri parent = folderDocumentUri(parentUri);
        Uri existing = findChild(parent, name);
        if (existing != null) return existing.toString();
        Uri created = DocumentsContract.createDocument(
                activity.getContentResolver(), parent,
                DocumentsContract.Document.MIME_TYPE_DIR, name);
        if (created == null) throw new Exception("Could not create game folder");
        return created.toString();
    }

    /** Atomically replace one file in a writable SAF directory. */
    public void writeDocument(String parentUri, String name, String mime,
                              InputStream input) throws Exception {
        Uri parent = folderDocumentUri(parentUri);
        Uri existing = findChild(parent, name);
        if (existing != null) {
            DocumentsContract.deleteDocument(activity.getContentResolver(), existing);
        }
        Uri created = DocumentsContract.createDocument(
                activity.getContentResolver(), parent,
                mime == null ? "application/octet-stream" : mime, name);
        if (created == null) throw new Exception("Could not create " + name);
        boolean complete = false;
        try (OutputStream output = activity.getContentResolver()
                 .openOutputStream(created, "w")) {
            if (output == null) throw new Exception("Could not write " + name);
            byte[] buffer = new byte[256 * 1024];
            int read;
            while ((read = input.read(buffer)) >= 0) {
                if (read > 0) output.write(buffer, 0, read);
            }
            complete = true;
        } finally {
            if (!complete) {
                try { DocumentsContract.deleteDocument(
                    activity.getContentResolver(), created); } catch (Exception ignored) {}
            }
        }
    }
}
