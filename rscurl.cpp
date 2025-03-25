/*****************************************************************************/
/* DLM модуль для RSL                                                        */   
/* curl                                                                      */
/* версия 0.2.0 (24.12.2023)                                                 */
/* tema@mail.ru                                                              */
/*****************************************************************************/

#include <windows.h>
#include <rsl/dlmintf.h>
#include <curl.h>

//HMODULE hLibCurl = NULL;
HINSTANCE hLibCurl = NULL;

typedef size_t (*func_write_callback_t)(char *, size_t, size_t, void *);
typedef size_t (*func_read_callback_t)(char *, size_t, size_t, void *);
typedef CURL * (*func_curl_easy_init_t) (void);

//curl_easy_setopt(CURL *handle, CURLOPT_URL, char *URL);
typedef CURLcode  (*func_curl_easy_setopt_t) (CURL *, CURLoption, void *);
typedef CURLcode  (*func_curl_easy_setopt_str_t) (CURL *, CURLoption, char *);
typedef CURLcode  (*func_curl_easy_setopt_callback_t) (CURL *, CURLoption, func_write_callback_t);
typedef CURLcode  (*func_curl_easy_h_t) (CURL *);
typedef const char * (*func_curl_easy_strerror_t) (CURLcode);
typedef CURLHcode (*func_curl_easy_header_t)(CURL *, const char *, size_t , unsigned int , int , struct curl_header **);

typedef CURLcode  (*func_curl_easy_getinfo_t) (CURL *, CURLINFO, ...);

typedef CURLUcode (*func_curl_url_set_t)(CURLU *, CURLUPart , const char *, unsigned int );

typedef void (*func_curl_slist_free_all_t)(struct curl_slist *);
typedef struct curl_slist * (*func_curl_slist_append_t)(struct curl_slist *, const char *);


// curl_easy_getinfo


typedef struct {
    char *buf;
    size_t size;
} mem_t;

// CURLcode curl_easy_setopt(CURL *handle, CURLOPT_WRITEFUNCTION, write_callback);
// CURLcode curl_easy_perform(CURL *easy_handle);

func_curl_easy_init_t            func_curl_easy_init = NULL;
func_curl_easy_setopt_str_t      func_curl_easy_setopt_str = NULL;
func_curl_easy_h_t               func_curl_easy_cleanup = NULL;
func_curl_easy_h_t               func_curl_easy_perform = NULL;
func_curl_easy_setopt_callback_t func_curl_easy_setopt_callback = NULL;
func_curl_easy_setopt_t          func_curl_easy_setopt = NULL;
func_curl_easy_strerror_t        func_curl_easy_strerror = NULL;
func_curl_easy_header_t          func_curl_easy_header = NULL;
func_curl_easy_getinfo_t         func_curl_easy_getinfo = NULL;
func_curl_url_set_t              func_curl_url_set = NULL;
func_curl_slist_free_all_t       func_curl_slist_free_all = NULL;
func_curl_slist_append_t         func_curl_slist_append = NULL;

size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    return fwrite(ptr, size, nmemb, stream);
}

size_t read_data(char *ptr, size_t size, size_t nmemb, FILE *stream) {
  return fread(ptr, size, nmemb, stream);
}


size_t grow_buffer(void *contents, size_t size, size_t nmemb, void *ctx) {
    print("grow_buffer %i %i\n", size, nmemb);
    size_t realsize = size * nmemb;
    mem_t *mem = (mem_t*) ctx;
    char *ptr = (char *)realloc(mem->buf, mem->size + realsize + 1);
    if (!ptr) {
        // out of memory 
        RslError("not enough memory");
        return 0;
    }
    mem->buf = ptr;
    memcpy(&(mem->buf[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->buf[mem->size] = '\0';

    //print("%s\n", mem->buf);

    return realsize;
}

char * rsGetStringParam(int iParam, char * defStr) {
    VALUE *vString;
    if (!GetParm (iParam,&vString) || vString->v_type != V_STRING) {
        if(defStr)
            return defStr;
        else
            RslError("Параметр №%i должен быть строкой",(iParam+1));
        }
    return vString->value.string;
}

char * rsGetFilePathParam(int iParam) {
    VALUE *vFilePath;
    if (!GetParm (iParam,&vFilePath) || vFilePath->v_type != V_STRING)
        RslError("Параметр №%i должен быть строкой",(iParam+1));
    char * sPath=(char *)malloc(sizeof(char)*strlen(vFilePath->value.string)+sizeof(char));
    OemToCharBuff(vFilePath->value.string, sPath, strlen(vFilePath->value.string));
    sPath[strlen(vFilePath->value.string)]='\0';
    return sPath;
}

static void CurlVersion(void) {
    typedef char * (*func_curl_version_t) (void);

    static func_curl_version_t curl_version = NULL;

    if (hLibCurl) {
       curl_version       = (func_curl_version_t)GetProcAddress(hLibCurl, "curl_version");
        if (!curl_version) {
            RslError("В библиотеке libcurl.dll не найдена функция curl_version");
        }

        ReturnVal(V_STRING, curl_version());

    }
            
}

static void CurlEasyStrError(void) {
    VALUE * vValue;

    if (GetParm(0, &vValue) && vValue->v_type == V_INTEGER) { 
        const char * s = func_curl_easy_strerror((CURLcode)vValue->value.intval);
        if (s)
            ReturnVal(V_STRING, (void *)s);

    }

}

class  CRScUrl {

public:

    CRScUrl (TGenObject *pThis = NULL) {

        ValueMake (&m_host);
        ValueSet (&m_host,V_STRING,"localhost");

        ValueMake (&m_port);
        m_port.v_type=V_INTEGER;
        m_port.value.intval=5672;
        
        ValueMake (&m_user);
        ValueSet (&m_user,V_STRING,"guest");

        ValueMake (&m_pass);
        ValueSet (&m_pass,V_STRING,"guest");

    }

    ~CRScUrl () {

        CURLcode curl_ret;

        func_curl_slist_free_all(headers);

        if (curl) 
            curl_ret = func_curl_easy_cleanup(curl);

    }

    RSL_CLASS(CRScUrl)

    RSL_INIT_DECL() {   
        VALUE *v;
        GetParm (*firstParmOffs, &v);

        if (!func_curl_easy_init) {
            func_curl_easy_init = (func_curl_easy_init_t)GetProcAddress(hLibCurl, "curl_easy_init");
            if (!func_curl_easy_init) {
                RslError("В библиотеке libcurl.dll не найдена функция curl_easy_init");
            }

            curl = func_curl_easy_init();

            //ReturnVal(V_MEMADDR, (void *)r);
        }
    }
    
    RSL_METHOD_DECL(EasyGetInfo) {

        VALUE * vInfo;
        CURLcode curl_ret;

        if (!GetParm(1, &vInfo) || vInfo->v_type != V_INTEGER)  {
            //RslError("Ошибка в параметре 2 (info)");
            return 0;
        }

        if (vInfo->value.intval > CURLINFO_STRING && vInfo->value.intval < CURLINFO_LONG) { // string
            char * s = NULL;

            if ((curl_ret = func_curl_easy_getinfo(curl, (CURLINFO)vInfo->value.intval, &s)) == CURLE_OK) {
                ReturnVal(V_STRING, (void *)s);
            }
        } else if (vInfo->value.intval > CURLINFO_LONG && vInfo->value.intval < CURLINFO_DOUBLE) { // int
            long l = 0;

            if ((curl_ret = func_curl_easy_getinfo(curl, (CURLINFO)vInfo->value.intval, &l)) == CURLE_OK) {
                ReturnVal(V_INTEGER, (void *)&l);
            }
        } else if (vInfo->value.intval > CURLINFO_OFF_T && vInfo->value.intval < 7340032) { // 64
            __int64 ll = 0;

            if ((curl_ret = func_curl_easy_getinfo(curl, (CURLINFO)vInfo->value.intval, &ll)) == CURLE_OK) {
                ll *= 10000;
                ValueSetAs(GetReturnVal(), V_RSLMONEY, V_MONEY, &ll);
            }
        }

        return 0;
    }

    RSL_METHOD_DECL(EasySetOptStr) {
        VALUE * vOption;
        VALUE * vString;

        CURLcode curl_ret;

        if (!GetParm(1, &vOption) || vOption->v_type != V_INTEGER || vOption->value.intval == 0) 
            RslError("Ошибка в параметре 2 (curl option)");

        if (!GetParm(2, &vString) || vString->v_type != V_STRING) 
            RslError("Ошибка в параметре 3 (parameter string)");

        curl_ret = func_curl_easy_setopt_str(curl, (CURLoption)(int)vOption->value.intval, vString->value.string);

        ReturnVal(V_INTEGER, (void *)&curl_ret);
        return 0;
                
    }

    RSL_METHOD_DECL(EasySetOpt) {
        VALUE * vOption;
        VALUE * vValue;

        CURLcode curl_ret;


        if (!GetParm(1, &vOption) || vOption->v_type != V_INTEGER || vOption->value.intval == 0) 
            RslError("Ошибка в параметре 2 (curl option)");

        if (!GetParm(2, &vValue)) 
            RslError("Ошибка в параметре 3");

        if (vOption->value.intval > 0 && vOption->value.intval < 10000) {
            if (vValue->v_type == V_INTEGER) {
                curl_ret = func_curl_easy_setopt(curl, (CURLoption)(int)vOption->value.intval, (void *)vValue->value.intval);
            } else {
                RslError("Ошибка типа параметра 3");
            }
        } else if (vOption->value.intval > 10000 && vOption->value.intval < 20000) {
            if (vValue->v_type == V_STRING) {
                curl_ret = func_curl_easy_setopt_str(curl, (CURLoption)(int)vOption->value.intval, vValue->value.string);
            } else {
                RslError("Ошибка типа параметра 3");
            }
        } else 
            return 0;

        ReturnVal(V_INTEGER, (void *)&curl_ret);
        return 0;
                
    }

    RSL_METHOD_DECL(EasySetWriteFile) {
        VALUE * vString;
        CURLcode curl_ret;

        if (!GetParm(1, &vString) || vString->v_type != V_STRING) 
            RslError("Ошибка в параметре 2 (parameter string)");

        if (f_in) {
            fclose(f_in);
            f_in = NULL;
        }

        if ((f_in = fopen(vString->value.string, "wb")) == NULL) {
            int ret = -1;
            ReturnVal(V_INTEGER, (void *)&ret);
            return 0;
        }

        if ((curl_ret = func_curl_easy_setopt_callback(curl, CURLOPT_WRITEFUNCTION, (func_write_callback_t)write_data)) > 0) {
            ReturnVal(V_INTEGER, (void *)&curl_ret);
            return 0;
        }

        curl_ret = func_curl_easy_setopt(curl, CURLOPT_WRITEDATA, f_in);
        ReturnVal(V_INTEGER, (void *)&curl_ret);
        return 0;
    }

    RSL_METHOD_DECL(EasySetReadFile) {
        VALUE * vString;
        CURLcode curl_ret;

        if (!GetParm(1, &vString) || vString->v_type != V_STRING) 
            RslError("Ошибка в параметре 2 (parameter string)");

        if (f_out) {
            fclose(f_out);
            f_out = NULL;
        }

        if ((f_out = fopen(vString->value.string, "rb")) == NULL) {
            int ret = -1;
            ReturnVal(V_INTEGER, (void *)&ret);
            return 0;
        }

        if ((curl_ret = func_curl_easy_setopt_callback(curl, CURLOPT_READFUNCTION, (func_read_callback_t)read_data)) > 0) {
            ReturnVal(V_INTEGER, (void *)&curl_ret);
            return 0;
        }

        curl_ret = func_curl_easy_setopt(curl, CURLOPT_READDATA, f_out);
        ReturnVal(V_INTEGER, (void *)&curl_ret);
        return 0;
    }

    RSL_METHOD_DECL(EasySetWriteString) {
        VALUE * vString;
        CURLcode curl_ret;

        if (mem_in != NULL) {

            if (mem_in->buf != NULL) {
                free(mem_in->buf);
                mem_in->buf = NULL;
                mem_in->size = 0;
            }

            free(mem_in);
            mem_in = NULL;
        }
        mem_in = (mem_t *)malloc(sizeof(mem_t));
        mem_in->size = 0;
        mem_in->buf = (char*)malloc(1);
        mem_in->buf[mem_in->size] = '\0';

        if ((curl_ret = func_curl_easy_setopt_callback(curl, CURLOPT_WRITEFUNCTION, (func_write_callback_t)grow_buffer)) > 0) {
            ReturnVal(V_INTEGER, (void *)&curl_ret);
            return 0;
        }

        curl_ret = func_curl_easy_setopt(curl, CURLOPT_WRITEDATA, mem_in);

        ReturnVal(V_INTEGER, (void *)&curl_ret);
        return 0;
    }


    RSL_METHOD_DECL(ClearWriteString) {

        if (mem_in != NULL) {

            if (mem_in->buf != NULL) {
                free(mem_in->buf);
                mem_in->buf = NULL;
            }

            mem_in->buf = (char*)malloc(1);
            mem_in->size = 0;
            mem_in->buf[mem_in->size] = '\0';

        }
        return 0;
    }

    RSL_METHOD_DECL(EasyGetHeader) {
        VALUE * vString; // header name
        VALUE * vIndex; // header index
        int idx = 0;
        CURLHcode curl_ret;
        struct curl_header * header;

        if (func_curl_easy_header == NULL) {
            func_curl_easy_header = (func_curl_easy_header_t)GetProcAddress(hLibCurl, "curl_easy_header");
            if (!func_curl_easy_header) 
                RslError("В библиотеке libcurl.dll не найдена функция curl_easy_header");
        }


        if (!GetParm(1, &vString) || vString->v_type != V_STRING) 
            RslError("Ошибка в параметре 1 (header name)");

        if (GetParm(2, &vIndex) && vIndex->v_type != V_INTEGER) 
            idx = vIndex->value.intval;

        if (CURLHE_OK == (curl_ret = func_curl_easy_header(curl, vString->value.string, idx, CURLH_HEADER, -1, &header))) {
            ReturnVal(V_STRING, (void *)header->value);
        }

        return 0;
    }

    RSL_METHOD_DECL(GetString) {
        if (mem_in == NULL) {
            ReturnVal(V_UNDEF, &mem_in);
        } else {
            if (mem_in->size) {
                ReturnVal(V_STRING, mem_in->buf);
            } else {
                char * sz = "";
                ReturnVal(V_STRING, sz);
            }
        }

        return 0;
    }


    RSL_METHOD_DECL(ClearString) {

        if (mem_in != NULL) {

            if (mem_in->buf != NULL) {
                free(mem_in->buf);
                mem_in->buf = NULL;
            }

            mem_in->buf = (char*)malloc(1);
            mem_in->size = 0;
            mem_in->buf[mem_in->size] = '\0';

        }
        return 0;
    }

    RSL_METHOD_DECL(AddHeader) {
        VALUE * vString; 
        struct curl_slist *tmp = NULL;
        int ret = 0;

        if (!GetParm(1, &vString) || vString->v_type != V_STRING) { 
            ReturnVal(V_UNDEF, &ret);
            return 0;
        }

        if (!func_curl_slist_append) {
            func_curl_slist_append = (func_curl_slist_append_t)GetProcAddress(hLibCurl, "curl_slist_append");
            if (!func_curl_slist_append) {
                RslError("В библиотеке libcurl.dll не найдена функция curl_slist_append");
                return 0;
            }
        }

        tmp = func_curl_slist_append(headers, vString->value.string);
        if (tmp) {
            headers = tmp;
        } else {
            func_curl_slist_free_all(headers);
            ReturnVal(V_UNDEF, &ret);
            return 0;
        }

        return 0;
    }


    RSL_METHOD_DECL(ClearHeaders) {
        func_curl_slist_free_all(headers);
        headers = NULL;
        return 0;
    }


    RSL_METHOD_DECL(EasyPerform) {
        CURLcode curl_ret;

        if (headers)
            func_curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        curl_ret = func_curl_easy_perform(curl);

        if (f_in) {
            fclose(f_in);
            f_in = NULL;
        }

        ReturnVal(V_INTEGER, (void *)&curl_ret);
        return 0;
                
    }

    /*
    RSL_METHOD_DECL(TestParam) {

        VALUE * vPart;
        VALUE * vString;
        VALUE * vFlags;

        CURLcode curl_ret;

        if (!GetParm(1, &vPart) || vPart->v_type != V_INTEGER) {
            ReturnVal(V_UNDEF, &curl_ret);
            return 0;
        }

        if (!GetParm(2, &vString) || vString->v_type != V_STRING) 
            ReturnVal(V_UNDEF, &curl_ret);
            return 0;
        }

        if (!GetParm(3, &vFlags) || vFlags->v_type != V_INTEGER) {
            ReturnVal(V_UNDEF, &curl_ret);
            return 0;
        }


        if (!func_curl_url_set) {
            func_curl_url_set = (func_curl_url_set_t)GetProcAddress(hLibCurl, "curl_url_set");
            if (!func_curl_url_set) {
                ReturnVal(V_UNDEF, &curl_ret);
                return 0;
            }
        }

        curl_ret = func_curl_url_set(curl, vPart->value.intval, vString->value.string, vFlags->value.intval);
        ReturnVal(V_INTEGER, &curl_ret);

        return 0;
                
    }
    */
    
    RSL_METHOD_DECL(TestParam) {
        VALUE *vParm;
       
        if (GetParm (1,&vParm) && vParm->v_type == V_STRING ){
            print (vParm->value.string);
            print ("\n");
            }
        else {
            print ("no param\n");
            }
        return 0;
    }


private:

    VALUE m_host;
    VALUE m_port;
    VALUE m_user;
    VALUE m_pass;

    CURL * curl = NULL;
    FILE * f_in = NULL;
    FILE * f_out = NULL;
    mem_t * mem_in = NULL;
    struct curl_slist *headers = NULL;
};

//TRslParmsInfo prmOneStr[] = { {V_STRING,0} };
//TRslParmsInfo prmTwoStr[] = { {V_STRING,0},{V_STRING,0} };
//TRslParmsInfo prmNo[] = {{}};

RSL_CLASS_BEGIN(CRScUrl)

    RSL_PROP_EX    (host,           m_host,          -1, V_STRING,  0)
    RSL_PROP_EX    (port,           m_port,          -1, V_INTEGER, 0)
    RSL_PROP_EX    (user,           m_user,          -1, V_STRING,  0)
    RSL_PROP_EX    (pass,           m_pass,          -1, V_STRING,  0)

    RSL_METH       (EasyGetInfo)
    RSL_METH       (EasySetOpt)
    RSL_METH       (EasySetOptStr)
    RSL_METH       (EasySetWriteFile) 
    RSL_METH       (EasySetWriteString)
    RSL_METH       (ClearWriteString)
    RSL_METH       (EasySetReadFile) 
    RSL_METH       (EasyGetHeader)
    RSL_METH       (EasyPerform)
    RSL_METH       (GetString)
    RSL_METH       (ClearString)
    RSL_METH       (AddHeader)
    RSL_METH       (ClearHeaders)

    RSL_INIT

RSL_CLASS_END  



EXP32 void DLMAPI EXP AddModuleObjects (void) {
    RslAddUniClass (CRScUrl::TablePtr,true);
    AddStdProc(V_STRING,  "CurlVersion",            CurlVersion,            0);
}




EXP32 void DLMAPI EXP InitExec (void)
{
    //hLibCrypto = LoadLibrary(nameLibCrypto);
    hLibCurl = LoadLibrary("libcurl.dll");

    if (!hLibCurl)
        RslError("Не найдена библиотека libcurl.dll (https://curl.se/windows/)");
        
    func_curl_easy_setopt = (func_curl_easy_setopt_t)GetProcAddress(hLibCurl, "curl_easy_setopt");
    if (!func_curl_easy_setopt) 
        RslError("В библиотеке libcurl.dll не найдена функция curl_easy_setopt");
    else {
        func_curl_easy_setopt_callback = (func_curl_easy_setopt_callback_t)func_curl_easy_setopt;
        func_curl_easy_setopt_str      = (func_curl_easy_setopt_str_t)     func_curl_easy_setopt;
    }

    func_curl_easy_cleanup = (func_curl_easy_h_t)GetProcAddress(hLibCurl, "curl_easy_cleanup");
    if (!func_curl_easy_cleanup) 
        RslError("В библиотеке libcurl.dll не найдена функция curl_easy_cleanup");

    func_curl_easy_perform = (func_curl_easy_h_t)GetProcAddress(hLibCurl, "curl_easy_perform");
    if (!func_curl_easy_perform) 
        RslError("В библиотеке libcurl.dll не найдена функция curl_easy_perform");

    func_curl_easy_strerror = (func_curl_easy_strerror_t)GetProcAddress(hLibCurl, "curl_easy_strerror");
    if (!func_curl_easy_strerror) 
        RslError("В библиотеке libcurl.dll не найдена функция curl_easy_strerror");

    func_curl_easy_getinfo = (func_curl_easy_getinfo_t)GetProcAddress(hLibCurl, "curl_easy_getinfo");
    if (!func_curl_easy_getinfo) 
        RslError("В библиотеке libcurl.dll не найдена функция curl_easy_getinfo");

    func_curl_slist_free_all = (func_curl_slist_free_all_t)GetProcAddress(hLibCurl, "curl_slist_free_all");
    if (!func_curl_easy_getinfo) 
        RslError("В библиотеке libcurl.dll не найдена функция curl_slist_free_all");
    

}

EXP32 void DLMAPI EXP DoneExec (void)
{
    if (hLibCurl) 
        FreeLibrary(hLibCurl);

}
