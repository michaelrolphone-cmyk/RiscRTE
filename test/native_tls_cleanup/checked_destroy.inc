inline bool checkedDestroy(Session& s){
  if(!s.tls)return !s.retained;
  auto* tls=s.tls;
  const int fd=tls->sockfd,wrapped=tls->server_fd.fd;
  // IDF initializes both to -1; before TLS setup wrapped is -1, after it
  // must name the same sole owned socket. Do not close ambiguous handles.
  if(fd < -1 || wrapped < -1 || (wrapped>=0 && wrapped!=fd))s.retained=true;
  else if(fd>=0 && ::close(fd)!=0)s.retained=true;
  // SDK normally discards mbedtls_net_free's close result. Detach before
  // SDK destruction so neither a failed close nor a freed fd is retried.
  tls->sockfd=-1;tls->server_fd.fd=-1;
  const int result=esp_tls_conn_destroy(tls);s.tls=nullptr;
  if(result!=0)s.retained=true;
  return !s.retained;
}
