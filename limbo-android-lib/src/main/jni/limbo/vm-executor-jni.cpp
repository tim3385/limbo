/*
 Copyright (C) Max Kastanas 2012

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <jni.h>
#include <stdio.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <malloc.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <unwind.h>
#include <dlfcn.h>
#include "vm-executor-jni.h"
#include "limbo_compat.h"

#define MSG_BUFSIZE 1024
#define MAX_STRING_LEN 1024

static int started = 0;
void * handle = 0;

void * loadLib(const char * lib_path_str) {

	char res_msg[MAX_STRING_LEN];
	sprintf(res_msg, "Loading lib: %s", lib_path_str);
	LOGV("%s", res_msg);
	void *ldhandle = dlopen(lib_path_str, RTLD_LAZY);
	return ldhandle;

}

extern "C" void run_tests();

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *pvt) {
	printf("* JNI_OnLoad called\n");

    //run_tests();
	return JNI_VERSION_1_2;
}


void set_qemu_var(JNIEnv* env, jobject thiz, const char * var, jint jvalue){

	dlerror();

	int value_int = (jint) jvalue;
	//LOGI("change var: %s = %d\n", var, value_int);
    void * obj = dlsym (handle, var);
    int * var_ptr = (int *) obj;
    *var_ptr = value_int;
}

JNIEXPORT void JNICALL Java_com_max2idea_android_limbo_jni_VMExecutor_setvncrefreshrate(
		JNIEnv* env, jobject thiz, jint jvalue) {
    set_qemu_var(env, thiz, "vnc_refresh_interval_inc", jvalue);
    set_qemu_var(env, thiz, "vnc_refresh_interval_base", jvalue);
}


JNIEXPORT void JNICALL Java_com_max2idea_android_limbo_jni_VMExecutor_setsdlrefreshrate(
		JNIEnv* env, jobject thiz, jint jvalue) {
    set_qemu_var(env, thiz, "gui_refresh_interval_default", jvalue);
}


int get_qemu_var(JNIEnv* env, jobject thiz, const char * var) {
    char res_msg[MSG_BUFSIZE + 1] = { 0 };

	dlerror();

    void * obj = dlsym (handle, var);
    const char *dlsym_error = dlerror();
    if (dlsym_error) {
        LOGE("Cannot load symbol %s: %s\n", var, dlsym_error);
    	return -1;
    }

    int * var_ptr = (int *) obj;
    //LOGD("Set Var %s: %s, %d\n", var, dlsym_error, *var_ptr);

    return *var_ptr;
}

JNIEXPORT jint JNICALL Java_com_max2idea_android_limbo_jni_VMExecutor_getsdlrefreshrate(
		JNIEnv* env, jobject thiz) {

    int res = get_qemu_var(env, thiz, "gui_refresh_interval_default");
    return res;
}


JNIEXPORT jint JNICALL Java_com_max2idea_android_limbo_jni_VMExecutor_getvncrefreshrate(
		JNIEnv* env, jobject thiz) {

    int res = get_qemu_var(env, thiz, "vnc_refresh_interval_inc");
    return res;
}



JNIEXPORT jstring JNICALL Java_com_max2idea_android_limbo_jni_VMExecutor_stop(
		JNIEnv* env, jobject thiz, jint jint_restart) {
	char res_msg[MSG_BUFSIZE + 1] = { 0 };

	int restart_int = jint_restart;

    if(restart_int) {
        typedef void (*reset_vm_t)(int);
        dlerror();
        reset_vm_t qemu_system_reset_request = (reset_vm_t) dlsym(handle, "qemu_system_reset_request");
        const char *dlsym_error = dlerror();
        if (dlsym_error) {
            LOGE("Cannot load symbol 'qemu_system_reset_request': %s\n", dlsym_error);
            return env->NewStringUTF(res_msg);
        }
        qemu_system_reset_request(6); //SHUTDOWN_CAUSE_GUEST_RESET
        sprintf(res_msg, "VM Restart Request");
    } else {
        typedef void (*stop_vm_t)(int);
        dlerror();
        stop_vm_t qemu_system_shutdown_request = (stop_vm_t) dlsym(handle, "qemu_system_shutdown_request");
        const char *dlsym_error = dlerror();
        if (dlsym_error) {
            LOGE("Cannot load symbol 'qemu_system_shutdown_request': %s\n", dlsym_error);
            return env->NewStringUTF(res_msg);
        }
        qemu_system_shutdown_request(3); //SHUTDOWN_CAUSE_HOST_SIGNAL
        sprintf(res_msg, "VM Stop Request");
	}

	LOGV("%s", res_msg);

	started = restart_int;

	return env->NewStringUTF(res_msg);
}


JNIEXPORT jstring JNICALL Java_com_max2idea_android_limbo_jni_VMExecutor_start(
        JNIEnv* env, jobject thiz,
		jstring storage_dir, jstring base_dir,
		jstring lib_path,
		jint sdl_scale_hint,
		jobjectArray params) {
	int res;
	char res_msg[MSG_BUFSIZE + 1] = { 0 };

	if (started) {
		sprintf(res_msg, "VM Already started");
		LOGV("%s", res_msg);
		return env->NewStringUTF(res_msg);
	}

	LOGV("Processing params");

	int MAX_PARAMS = 256;
	int argc = 0;
	char ** argv;

	//params
	argc = env->GetArrayLength(params);
    //LOGV("Params = %d", argc);

	argv = (char **) malloc((argc + 1) * sizeof(*argv));

	for (int i = 0; i < argc; i++) {
        jstring string = (jstring)(env->GetObjectArrayElement(params, i));
		const char *param_str = env->GetStringUTFChars(string, 0);
		int length = strlen(param_str)+1;
        argv[i] = (char *) malloc(length * sizeof(char));
		strcpy(argv[i], param_str);
		env->ReleaseStringUTFChars(string, param_str);
	}

	//XXX: Do not remove
	argv[argc] = NULL;

	for (int k = 0; k < argc; k++) {
		//LOGV("param[%d]=%s", k, argv[k]);
	}

	started = 1;

	printf("Starting VM...");


//LOAD LIB
	const char *lib_path_str = NULL;
	if (lib_path != NULL)
		lib_path_str = env->GetStringUTFChars(lib_path, 0);

	if (handle == NULL) {
		handle = loadLib(lib_path_str);
	}

	if (!handle) {
		sprintf(res_msg, "Error opening lib: %s :%s", lib_path_str, dlerror());
		LOGV("%s", res_msg);
		return env->NewStringUTF(res_msg);
	}

	setup_jni(env, thiz, storage_dir, base_dir);

    set_qemu_var(env, thiz, "limbo_sdl_scale_hint", sdl_scale_hint);

	LOGV("Loading symbol main...\n");
	typedef void (*main_t)(int argc, char **argv, char **envp);

	// reset errors
	dlerror();
	main_t qemu_main = (main_t) dlsym(handle, "main");
	const char *dlsym_error = dlerror();
	if (dlsym_error) {
		LOGE("Cannot load qemu symbol 'main': %s\n", dlsym_error);
		dlclose(handle);
		handle = NULL;
		return env->NewStringUTF(dlsym_error);
	}

	qemu_main(argc, argv, NULL);

	//UNLOAD LIB
	sprintf(res_msg, "Closing lib: %s", lib_path_str);
	LOGV("%s", res_msg);
	dlclose(handle);
	handle = NULL;
	started = 0;

    env->ReleaseStringUTFChars(lib_path, lib_path_str);


	sprintf(res_msg, "VM shutdown");
	LOGV("%s", res_msg);
    return env->NewStringUTF(res_msg);
}
}

void setup_jni(JNIEnv* env, jobject thiz, jstring storage_dir, jstring base_dir) {

    const char *base_dir_str = NULL;
    const char *storage_dir_str = NULL;

    if (base_dir != NULL)
		base_dir_str = env->GetStringUTFChars(base_dir, 0);

    if (storage_dir != NULL)
		storage_dir_str = env->GetStringUTFChars(storage_dir, 0);

	jclass c = env->GetObjectClass(thiz);
	set_jni(env, thiz, c, storage_dir_str, base_dir_str);
}

