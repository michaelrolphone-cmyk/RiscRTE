/* Exact IDF 38eeba213a functions; Apache-2.0. */
void esp_mbedtls_conn_delete(esp_tls_t *tls)
{
    if (tls != NULL) {
        esp_mbedtls_cleanup(tls);
        if (tls->is_tls) {
            if (tls->server_fd.fd != -1) {
                mbedtls_net_free(&tls->server_fd);
                /* Socket is already closed by `mbedtls_net_free` and hence also change assignment of its copy to an invalid value */
                tls->sockfd = -1;
            }
        }
    }
}

int esp_tls_conn_destroy(esp_tls_t *tls)
{
    if (tls != NULL) {
        int ret = 0;
        _esp_tls_conn_delete(tls);
        if (tls->sockfd >= 0) {
            ret = close(tls->sockfd);
        }
        esp_tls_internal_event_tracker_destroy(tls->error_handle);
        free(tls);
        return ret;
    }
    return -1; // invalid argument
}
