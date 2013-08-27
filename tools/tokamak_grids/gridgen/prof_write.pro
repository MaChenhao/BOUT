;filename is both the input grid file and output, 
;so NOTICE that BACKUP the grid file before this process

;Process to write density profile into grid file with tanh function. 
;height is the top value of the density profile, bottom is the smallest value
;of the profile. temperature is derived from P/n. The density profile
;is name as "Niexp" in output grid, temperatures are named as "Tiexp"
;and "Teexp". The "Nixexp" is the value of density at the inner
;boundary. The default density unit is 1e20 m^{-3}

 
pro prof_write, height=height, bottom=bottom, filename=filename

  ON_ERROR, 2

  IF NOT KEYWORD_SET(height) THEN height = 0.3
  IF NOT KEYWORD_SET(bottom) THEN bottom = 0.02
  IF NOT KEYWORD_SET(filename) THEN begin 
     print, "Please input grid file name!"
  endif

  kb = 1.38e-23
  sep_circlie = 0.81
  density = 1e20
  ev_k = 11605.

  g=file_import(filename)
  nx = g.nx
  ny = g.ny
  ixsep = g.ixseps1
  jysep1 = g.jyseps1_1
  jysep2 = g.jyseps2_2

  result = fltarr(nx, ny)
  maxr = max(g.rxy[nx-1,*], kk)
  maxy = kk

  dp = -deriv(g.pressure[*,maxy])
  maxp = max(dp, k)
  center = k
  for i=1, nx-2 do begin
     if (dp[i] lt maxp*0.75) and (dp[i+1] gt maxp*0.75) then begin
        ixsmall = i
     endif
     if (dp[i] gt maxp*0.75) and (dp[i+1] lt maxp*0.75) then begin
        ixbig = i
     endif
  endfor
  width = ixbig-ixsmall
  
  tmpt = exp(float(-center)/width)
  dmpt = (tmpt - 1.0/tmpt) / (tmpt + 1.0/tmpt)
  h_real = 2.*(height - bottom) / (1. - dmpt)

;  print, ixsep, jysep1, jysep2, maxy, center, width

  if (jysep1 gt 0) then begin  ;single null geomtry
     xlimit = ixsep+1
     for i=0, nx-1 do begin
        for j=0, ny-1 do begin
           mgx = i
           if (mgx gt xlimit) or (j le jysep1) or (j gt jysep2 ) then begin
              mgx = xlimit
           endif
;           print,mgx, xlimit, j, jysep1, jysep2
           rlx = mgx - center
           temp = exp(float(rlx)/width)
           dampr = (temp - 1.0/temp) / (temp + 1.0/temp)
           result[i,j] = 0.5*(1.0 - dampr) * h_real + bottom
;           print,rlx, width, float(rlx)/width, temp, dampr
        endfor
     endfor
  endif else begin             ;circular geometry
     for i=0, nx-1 do begin
        mgx = float(i)
        xlimit = sep_circle * nx
        if (mgx gt xlimit) then begin
           mgx =xlimit
        endif
        rlx = mgx - center
        temp = exp(rlx/width)
        dampr = (temp - 1.0/temp) / (temp + 1.0/temp)        
        for j=0, ny-1 do begin
           result[i,j] = 0.5*(1.0 - dampr) * h_real + bottom
        endfor
     endfor
  endelse

  profn = result*density
  proft= g.pressure/(kb*profn*ev_k)/2.

  window,0
  surface,result,chars=3, title="N!ii0!n (10!e20!n m!e-3!n)"
  window,1
  ;plot,result[*,maxy],chars=1.5
  surface,proft,chars=3, title="T!ii0!n (eV)"

  handle = file_open(filename, /write)
  s = file_write(handle, 'Niexp', result)
  s = file_write(handle, 'Tiexp', proft)
  s = file_write(handle, 'Teexp', proft)
  s = file_write(handle, 'Nixexp', result[0,maxy])
  file_close, handle

end     
           
