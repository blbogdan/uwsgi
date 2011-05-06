#include "psgi.h"

extern struct uwsgi_server uwsgi;

/* statistically ordered */
struct http_status_codes hsc[] = {
        {"200", "OK"},
        {"302", "Found"},
        {"404", "Not Found"},
        {"500", "Internal Server Error"},
        {"301", "Moved Permanently"},
        {"304", "Not Modified"},
        {"303", "See Other"},
        {"403", "Forbidden"},
        {"307", "Temporary Redirect"},
        {"401", "Unauthorized"},
        {"400", "Bad Request"},
        {"405", "Method Not Allowed"},
        {"408", "Request Timeout"},

        {"100", "Continue"},
        {"101", "Switching Protocols"},
        {"201", "Created"},
        {"202", "Accepted"},
        {"203", "Non-Authoritative Information"},
        {"204", "No Content"},
        {"205", "Reset Content"},
        {"206", "Partial Content"},
        {"300", "Multiple Choices"},
        {"305", "Use Proxy"},
        {"402", "Payment Required"},
        {"406", "Not Acceptable"},
        {"407", "Proxy Authentication Required"},
        {"409", "Conflict"},
        {"410", "Gone"},
        {"411", "Length Required"},
        {"412", "Precondition Failed"},
        {"413", "Request Entity Too Large"},
        {"414", "Request-URI Too Long"},
        {"415", "Unsupported Media Type"},
        {"416", "Requested Range Not Satisfiable"},
        {"417", "Expectation Failed"},
        {"501", "Not Implemented"},
        {"502", "Bad Gateway"},
        {"503", "Service Unavailable"},
        {"504", "Gateway Timeout"},
        {"505", "HTTP Version Not Supported"},
        { "", NULL },
};


int psgi_response(struct wsgi_request *wsgi_req, PerlInterpreter *my_perl, AV *response) {

	SV **status_code, **hitem ;
	AV *headers, *body =NULL;
	STRLEN hlen;
	struct http_status_codes *http_sc;
	int i,vi, base;
	char *chitem;

#ifdef UWSGI_ASYNC
	if (wsgi_req->async_status == UWSGI_AGAIN) {

		wsgi_req->async_force_again = 0;

		wsgi_req->switches++;
                SV *chunk = uwsgi_perl_obj_call(wsgi_req->async_placeholder, "getline");
		if (!chunk) {
			internal_server_error(wsgi_req, "exception raised");
			return UWSGI_OK;
		}

		if (wsgi_req->async_force_again) {
			SvREFCNT_dec(chunk);
			return UWSGI_AGAIN;
		}

                chitem = SvPV( chunk, hlen);

                if (hlen <= 0) {
			SvREFCNT_dec(chunk);
			SV *closed = uwsgi_perl_obj_call(wsgi_req->async_placeholder, "close");
                	if (closed) {
                        	SvREFCNT_dec(closed);
                	}

			SvREFCNT_dec(wsgi_req->async_environ);
        		SvREFCNT_dec(wsgi_req->async_result);

			return UWSGI_OK;
                }

                wsgi_req->response_size += wsgi_req->socket->proto_write(wsgi_req, chitem, hlen);
		SvREFCNT_dec(chunk);

		return UWSGI_AGAIN;
	}
#endif

	status_code = av_fetch(response, 0, 0);
	if (!status_code) { uwsgi_log("invalid PSGI status code\n"); return UWSGI_OK;}

        wsgi_req->hvec[0].iov_base = "HTTP/1.1 ";
        wsgi_req->hvec[0].iov_len = 9;

        wsgi_req->hvec[1].iov_base = SvPV(*status_code, hlen);

        wsgi_req->hvec[1].iov_len = 3;

        wsgi_req->status = atoi(wsgi_req->hvec[1].iov_base);

        wsgi_req->hvec[2].iov_base = " ";
        wsgi_req->hvec[2].iov_len = 1;

        wsgi_req->hvec[3].iov_len = 0;

        // get the status code
        for (http_sc = hsc; http_sc->message != NULL; http_sc++) {
                if (!strncmp(http_sc->key, wsgi_req->hvec[1].iov_base, 3)) {
                        wsgi_req->hvec[3].iov_base = (char *) http_sc->message;
                        wsgi_req->hvec[3].iov_len = http_sc->message_size;
                        break;
                }
        }

        if (wsgi_req->hvec[3].iov_len == 0) {
                wsgi_req->hvec[3].iov_base = "Unknown";
                wsgi_req->hvec[3].iov_len =  7;
        }

        wsgi_req->hvec[4].iov_base = "\r\n";
        wsgi_req->hvec[4].iov_len = 2;

        hitem = av_fetch(response, 1, 0);
	if (!hitem) { uwsgi_log("invalid PSGI headers\n"); return UWSGI_OK;}

        headers = (AV *) SvRV(*hitem);
	if (!headers) { uwsgi_log("invalid PSGI headers\n"); return UWSGI_OK;}

        base = 5;


        // put them in hvec
        for(i=0; i<=av_len(headers); i++) {

                vi = (i*2)+base;
                hitem = av_fetch(headers,i,0);
                chitem = SvPV(*hitem, hlen);
                wsgi_req->hvec[vi].iov_base = chitem; wsgi_req->hvec[vi].iov_len = hlen;

                wsgi_req->hvec[vi+1].iov_base = ": "; wsgi_req->hvec[vi+1].iov_len = 2;

                hitem = av_fetch(headers,i+1,0);
                chitem = SvPV(*hitem, hlen);
                wsgi_req->hvec[vi+2].iov_base = chitem; wsgi_req->hvec[vi+2].iov_len = hlen;

                wsgi_req->hvec[vi+3].iov_base = "\r\n"; wsgi_req->hvec[vi+3].iov_len = 2;

                wsgi_req->header_cnt++;

                i++;
        }

	vi = (i*2)+base;
        wsgi_req->hvec[vi].iov_base = "\r\n"; wsgi_req->hvec[vi].iov_len = 2;

        if ( !(wsgi_req->headers_size += wsgi_req->socket->proto_writev_header(wsgi_req, wsgi_req->hvec, vi+1)) ) {
                uwsgi_error("writev()");
        }

        hitem = av_fetch(response, 2, 0);

	if (!hitem) {
		return UWSGI_OK;
	}

	if (!SvRV(*hitem)) { uwsgi_log("invalid PSGI response body\n") ; return UWSGI_OK; }
	
        if (SvTYPE(SvRV(*hitem)) == SVt_PVGV || SvTYPE(SvRV(*hitem)) == SVt_PVHV || SvTYPE(SvRV(*hitem)) == SVt_PVMG) {

		// respond to fileno ?
		if (uwsgi.async < 2) {
			if (uwsgi_perl_obj_can(*hitem, "fileno", 6)) {
				SV *fn = uwsgi_perl_obj_call(*hitem, "fileno");
				wsgi_req->sendfile_fd = SvIV(fn);
				SvREFCNT_dec(fn);	
				wsgi_req->response_size += uwsgi_sendfile(wsgi_req);
				return UWSGI_OK;
			}
		}

                for(;;) {

			wsgi_req->switches++;
                        SV *chunk = uwsgi_perl_obj_call(*hitem, "getline");
			if (!chunk) {
				internal_server_error(wsgi_req, "exception raised");
				break;
			}
                        chitem = SvPV( chunk, hlen);
#ifdef UWSGI_ASYNC
			if (uwsgi.async > 1 && wsgi_req->async_force_again) {
				SvREFCNT_dec(chunk);
				wsgi_req->async_status = UWSGI_AGAIN;
				wsgi_req->async_placeholder = (SV *) *hitem;
				return UWSGI_AGAIN;
			}
#endif
                        if (hlen <= 0) {
				SvREFCNT_dec(chunk);
                                break;
                        }
                        wsgi_req->response_size += wsgi_req->socket->proto_write(wsgi_req, chitem, hlen);
			SvREFCNT_dec(chunk);
#ifdef UWSGI_ASYNC
			if (uwsgi.async > 1) {
				wsgi_req->async_status = UWSGI_AGAIN;
				wsgi_req->async_placeholder = (SV *) *hitem;
				return UWSGI_AGAIN;
			}
#endif
                }

		SV *closed = uwsgi_perl_obj_call(*hitem, "close");
		if (closed) {
			SvREFCNT_dec(closed);
		}

        }
        else if (SvTYPE(SvRV(*hitem)) == SVt_PVAV)  {

                body = (AV *) SvRV(*hitem);

                for(i=0; i<=av_len(body); i++) {
                        hitem = av_fetch(body,i,0);
                        chitem = SvPV(*hitem, hlen);
                        wsgi_req->response_size += wsgi_req->socket->proto_write(wsgi_req, chitem, hlen);
                }

        }
        else {
                uwsgi_log("unsupported response body type: %d\n", SvTYPE(SvRV(*hitem)));
        }
	
	return UWSGI_OK;

}
