;Import the experimentally measured density profiles, inarr, into the
;grid file, g. The input profile should be interpolated to the grid
;before this code.(ver 0.1). This function is only for single null geometry

pro Ni2Grid2, inarr, g, filename
  
  ON_ERROR, 2
  
  s=size(inarr)
  IF s[0] NE 1 THEN BEGIN
     PRINT,"input data should be 1D!"
  ENDIF

  nin = s[1]

  nx = g.nx
  ny = g.ny
  if nin NE nx then begin
     print, "input data has wrong number of elements."
  endif

  kb = 1.38e-23
  profn = fltarr(nx,ny)
  xsep = floor(g.ixseps1)

  for i=0, xsep do begin
     for j=0, ny-1 do begin
        if (j gt g.jyseps1_1) and (j le g.jyseps2_2) then begin
           profn[i,j] = inarr[i]
        endif else begin
           profn[i,j] = inarr[xsep]
        endelse
     endfor
  endfor

  for i=xsep, nx-1 do begin
     for j=0, ny-1 do begin
        profn[i,j] = inarr[xsep]
     endfor
  endfor

  proft = g.pressure/(kb*profn*11605.)/2.
  profn = profn/1e20

  gnew = g
  gnew.ni0 = profn
  gnew.ti0 = proft
  gnew.te0 = proft
  gnew.ni_x = profn[0,ny/2]

;  f=file_export(filename, gnew)
  handle = file_open(filename, /write)
  s = file_write(handle, 'Niexp', profn)
  s = file_write(handle, 'Tiexp', proft)
  s = file_write(handle, 'Teexp', proft)
  s = file_write(handle, 'Nixexp', profn[0,ny/2])
  file_close, handle

end

  
