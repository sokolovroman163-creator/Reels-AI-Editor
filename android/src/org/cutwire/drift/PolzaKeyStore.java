package org.cutwire.drift;

import android.content.Context;
import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;
import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import java.util.Arrays;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

/** Only ciphertext/IV leave the Keystore boundary. Never log secrets or exceptions. */
public final class PolzaKeyStore {
    private static final String ALIAS = "reelsai.polza.aes.v1";
    private static final String PREFS = "reelsai_polza_secret";
    private PolzaKeyStore() { }
    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }
    private static KeyStore store() throws Exception {
        KeyStore result = KeyStore.getInstance("AndroidKeyStore");
        result.load(null);
        return result;
    }
    public static synchronized boolean has(Context context) {
        try {
            return prefs(context).contains("ciphertext") && store().containsAlias(ALIAS);
        } catch (Exception ignored) { return false; }
    }
    public static synchronized boolean save(Context context, String value) {
        byte[] plain = value.getBytes(StandardCharsets.UTF_8);
        try {
            if (plain.length == 0 || plain.length > 4096) return false;
            KeyStore keys = store();
            SecretKey key = (SecretKey) keys.getKey(ALIAS, null);
            if (key == null) {
                KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
                generator.init(new KeyGenParameterSpec.Builder(ALIAS,
                    KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                    .setKeySize(256).setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .setRandomizedEncryptionRequired(true).build());
                key = generator.generateKey();
            }
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.ENCRYPT_MODE, key);
            byte[] encrypted = cipher.doFinal(plain);
            return prefs(context).edit()
                .putString("ciphertext", Base64.encodeToString(encrypted, Base64.NO_WRAP))
                .putString("iv", Base64.encodeToString(cipher.getIV(), Base64.NO_WRAP)).commit();
        } catch (Exception ignored) { return false; }
        finally { Arrays.fill(plain, (byte) 0); }
    }
    /** JNI caller consumes these bytes only immediately before creating a request. */
    public static synchronized byte[] read(Context context) {
        try {
            if (!has(context)) return new byte[0];
            SharedPreferences preferences = prefs(context);
            byte[] iv = Base64.decode(preferences.getString("iv", ""), Base64.NO_WRAP);
            byte[] encrypted = Base64.decode(preferences.getString("ciphertext", ""), Base64.NO_WRAP);
            if (iv.length != 12 || encrypted.length < 16) return new byte[0];
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, (SecretKey) store().getKey(ALIAS, null), new GCMParameterSpec(128, iv));
            return cipher.doFinal(encrypted);
        } catch (Exception ignored) { return new byte[0]; }
    }
    public static synchronized boolean delete(Context context) {
        boolean cleared = false;
        try { cleared = prefs(context).edit().clear().commit(); }
        catch (Exception ignored) { /* Still destroy the encryption key. */ }
        try {
            KeyStore keys = store();
            if (keys.containsAlias(ALIAS)) keys.deleteEntry(ALIAS);
            return cleared;
        } catch (Exception ignored) { return false; }
    }
}
